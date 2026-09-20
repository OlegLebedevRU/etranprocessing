import React, { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  Alert,
  Card,
  Drawer,
  Grid,
  message,
  Space,
  theme,
  Typography,
} from "antd";
import {
  DeviceListItem,
  getDevices,
} from "../api/devices";
import {
  acquireControlLease,
  CameraSource,
  changeControlScope,
  ClickResult,
  createVideoSession,
  DeviceInventory,
  DisplaySource,
  getControlStatus,
  getDeviceInventory,
  getDeviceStreamState,
  getJanusWsUrl,
  getVideoSessionStatus,
  keepaliveControlLease,
  releaseControlLease,
  startDeviceStream,
  stopDeviceStream,
  StreamPresenceInfo,
} from "../api/video";
import { SessionLifecycleCoordinator } from "../utils/sessionLifecycle";
import { listTerminalsSettings } from "../api/settings";
import { JanusStreamingClient } from "../api/janusClient";
import { useSession } from "../session/SessionContext";
import PageHeader from "../components/PageHeader";
import { useRemoteControl } from "../hooks/useRemoteControl";
import { hasPermission, PERMISSION_VIDEO_VIEW } from "../utils/permissions";

// Новые переработанные UX/UI компоненты
import { TerminalList } from "../components/video/TerminalList";
import { TerminalHeader } from "../components/video/TerminalHeader";
import { VideoPlayerScreen } from "../components/video/VideoPlayerScreen";
import { StreamControls, StreamStage } from "../components/video/StreamControls";
import { SourceSelector } from "../components/video/SourceSelector";
import { RemoteControlPanel } from "../components/video/RemoteControlPanel";

const { Text } = Typography;

/**
 * Преобразование ошибок терминала и бэкенда в понятные сообщения на русском языке
 */
function formatVideoError(err: any): { title: string; message: string } {
  const detail = err?.response?.data?.detail;
  const status = err?.response?.status;
  const raw = typeof detail === "string" ? detail : (detail?.message || err?.message || "");

  if (status === 403) {
    return {
      title: "Доступ ограничен",
      message: raw || "У вас нет прав для просмотра или управления видеотрансляцией на данном терминале.",
    };
  }
  if (status === 404) {
    return {
      title: "Терминал не найден",
      message: "Устройство не найдено или удалено из реестра.",
    };
  }
  if (
    status === 409 ||
    raw.includes("lease_taken") ||
    raw.includes("busy") ||
    raw.includes("session_busy")
  ) {
    return {
      title: "Терминал занят",
      message:
        raw ||
        "Терминал уже находится под управлением другого пользователя или занят другой сессией.",
    };
  }
  if (raw.includes("offline") || raw.includes("Device is offline")) {
    return {
      title: "Терминал не в сети",
      message: "Терминал не на связи (offline). Проверьте питание и подключение к сети.",
    };
  }
  if (raw.includes("source_unavailable") || raw.includes("source")) {
    return {
      title: "Источник недоступен",
      message: "Выбранный экран или камера недоступны на терминале. Выберите другой источник.",
    };
  }
  if (raw.includes("ffmpeg_missing")) {
    return {
      title: "Компонент не найден",
      message: "На терминале отсутствует утилита захвата видео ffmpeg.",
    };
  }
  if (raw.includes("terminal_timeout") || raw.includes("timeout")) {
    return {
      title: "Таймаут соединения",
      message: "Терминал не ответил на команду запуска в установленное время.",
    };
  }

  return {
    title: "Ошибка запуска трансляции",
    message: raw || "Не удалось запустить видеопоток. Повторите попытку через несколько секунд.",
  };
}

export default function VideoSurveillancePage() {
  const { user, loading: userLoading } = useSession();
  const { token } = theme.useToken();
  const screens = Grid.useBreakpoint();
  const isMobile = !screens.md;
  const isDesktop = Boolean(screens.lg);

  const orgId = typeof user?.org_id === "number" ? user.org_id : 1;

  // Определение роли: Viewer vs Operator / Admin
  const isViewer = user?.role_id === 4 || user?.role === "viewer";
  const canView = hasPermission(user, PERMISSION_VIDEO_VIEW);
  const isOperator =
    !isViewer &&
    Boolean(
      user?.role_id === 1 ||
        user?.role_id === 2 ||
        user?.role_id === 3 ||
        user?.role_id === 5 ||
        user?.is_superuser ||
        user?.role === "superuser" ||
        user?.role === "admin"
    );

  // Список устройств и словарь адресов (device_id -> address)
  const [devices, setDevices] = useState<DeviceListItem[]>([]);
  const [terminalAddresses, setTerminalAddresses] = useState<Record<number, string>>({});
  const [loadingDevices, setLoadingDevices] = useState<boolean>(true);
  const [selectedDevice, setSelectedDevice] = useState<DeviceListItem | null>(null);

  // Мобильный / планшетный Drawer выбора терминала
  const [isDeviceDrawerOpen, setIsDeviceDrawerOpen] = useState(false);

  // Источники видео (дисплеи, камеры)
  const [inventory, setInventory] = useState<DeviceInventory | null>(null);
  const [loadingInventory, setLoadingInventory] = useState<boolean>(false);
  const [selectedSourceKey, setSelectedSourceKey] = useState<string>("");
  const [selectedProfile, setSelectedProfile] = useState<string>("low");

  // Статус потока
  const [activeStream, setActiveStream] = useState<StreamPresenceInfo | null>(null);
  const [streamStage, setStreamStage] = useState<StreamStage>("idle");
  const [bannerError, setBannerError] = useState<{
    code: string;
    message: string;
  } | null>(null);
  const [refusalNotice, setRefusalNotice] = useState<string | null>(null);

  // Медиаплеер и WebRTC сессия
  const [isSessionActive, setIsSessionActive] = useState<boolean>(false);
  const [statusText, setStatusText] = useState<string>("Не запущена");
  const [activeLeaseId, setActiveLeaseId] = useState<string | null>(null);

  const videoRef = useRef<HTMLVideoElement | null>(null);
  const janusClientRef = useRef<JanusStreamingClient | null>(null);
  const pollTimerRef = useRef<any>(null);
  const keepaliveTimerRef = useRef<any>(null);
  const leaseRef = useRef<{ id: string; deviceId: number } | null>(null);
  const prevPacketsRef = useRef<{ packets: number; time: number } | null>(null);

  // Загрузка терминалов и их адресов
  const loadDevices = useCallback(async () => {
    setLoadingDevices(true);
    try {
      const [res, settingsData] = await Promise.all([
        getDevices(orgId, { page: 1, size: 100 }),
        listTerminalsSettings(orgId).catch(() => ({ items: [] })),
      ]);
      const settingsList = "items" in settingsData ? settingsData.items : [];

      let allItems = res.items || [];
      if (res.pages > 1) {
        for (let p = 2; p <= res.pages; p++) {
          const nextRes = await getDevices(orgId, { page: p, size: 100 });
          allItems = allItems.concat(nextRes.items || []);
        }
      }

      // Формирование словаря адресов: device_id -> address
      const addrMap: Record<number, string> = {};
      for (const t of settingsList) {
        if (t.device_id && t.address) {
          addrMap[t.device_id] = t.address;
        }
      }
      for (const d of allItems) {
        if (!addrMap[d.device_id] && Array.isArray(d.tags)) {
          const addrTag = d.tags.find(
            (t) => t.tag === "address" || t.tag === "location" || t.tag === "addr"
          );
          if (addrTag?.value) addrMap[d.device_id] = addrTag.value;
        }
      }

      setTerminalAddresses(addrMap);
      setDevices(allItems);

      setSelectedDevice((prev) => {
        if (prev && allItems.some((d) => d.device_id === prev.device_id)) {
          return prev;
        }
        return allItems.length > 0 ? allItems[0] : null;
      });
    } catch (err: any) {
      message.error(err?.message || "Ошибка загрузки списка терминалов");
    } finally {
      setLoadingDevices(false);
    }
  }, [orgId]);

  useEffect(() => {
    if (!userLoading) {
      loadDevices();
    }
  }, [loadDevices, userLoading]);

  // Обработчики кликов удалённого управления
  const handleClickResult = useCallback((res: ClickResult) => {
    // Компактный статус отображается в панели RemoteControlPanel; не показываем success-toast на каждый клик
    if (res.result === "unconfirmed") {
      message.warning(res.message || "Результат ввода не подтвержден. Проверьте изображение перед повторным действием.");
    }
  }, []);

  const handleErrorMessage = useCallback((msg: string) => {
    message.error(msg);
  }, []);

  // Хук удалённого управления (мышь/клавиатура)
  const lastPacketGrowthTimeRef = useRef<number>(Date.now());

  // Координатор сессии управляет generation/эпохами, keepalive и событиями терминала
  const coordinatorRef = useRef<SessionLifecycleCoordinator>(
    new SessionLifecycleCoordinator({
      onKeepaliveRequest: async (deviceId, leaseId) => {
        await keepaliveControlLease(deviceId, leaseId);
      },
      onReleaseRequest: async (deviceId, leaseId) => {
        await releaseControlLease(deviceId, leaseId);
      },
      onScopeChangeRequest: async (deviceId, scope) => {
        await changeControlScope(deviceId, scope);
      },
      onSessionTerminated: (reason, details) => {
        message.warning(`Трансляция остановлена терминалом: ${reason || "остановлена"}`);
        setStatusText(`Остановлена (${reason || "терминал"})`);
        setStreamStage(details?.code === "failed" ? "failed" : "idle");
        setIsSessionActive(false);
        setActiveStream(null);
        setActiveLeaseId(null);
        leaseRef.current = null;
        if (videoRef.current) {
          videoRef.current.srcObject = null;
        }
        if (janusClientRef.current) {
          void janusClientRef.current.stop();
          janusClientRef.current = null;
        }
        if (pollTimerRef.current) {
          clearInterval(pollTimerRef.current);
          pollTimerRef.current = null;
        }
      },
      onLeaseLost: (statusCode, msg) => {
        message.error(`Аренда видеопотока завершена (${statusCode}): ${msg}. Сессия остановлена.`);
        setStatusText(`Сессия завершена (${statusCode})`);
        setStreamStage("failed");
        setIsSessionActive(false);
        setActiveStream(null);
        setActiveLeaseId(null);
        leaseRef.current = null;
        if (videoRef.current) {
          videoRef.current.srcObject = null;
        }
        if (janusClientRef.current) {
          void janusClientRef.current.stop();
          janusClientRef.current = null;
        }
        if (pollTimerRef.current) {
          clearInterval(pollTimerRef.current);
          pollTimerRef.current = null;
        }
      },
    })
  );

  const rc = useRemoteControl({
    deviceId: selectedDevice?.device_id ?? null,
    isSessionActive,
    leaseId: activeLeaseId,
    streamMode: activeStream?.mode,
    sharedLease: true,
    onStreamState: (event) => {
      coordinatorRef.current.handleStreamStateEvent(event);
    },
    onLeaseLost: (reason) => {
      void coordinatorRef.current.stopSession(`lease_revoked_${reason}`);
    },
    onClickResult: handleClickResult,
    onErrorMessage: handleErrorMessage,
  });

  const rcRef = useRef(rc);
  rcRef.current = rc;

  // Остановка текущей медиасессии
  const stopSession = useCallback(async (reason = "normal") => {
    await rcRef.current.disable("session_stopped");
    await coordinatorRef.current.stopSession(reason);

    if (pollTimerRef.current) {
      clearInterval(pollTimerRef.current);
      pollTimerRef.current = null;
    }
    prevPacketsRef.current = null;

    if (janusClientRef.current) {
      try {
        await janusClientRef.current.stop();
      } catch (err) {
        console.error("Error stopping Janus client:", err);
      }
      janusClientRef.current = null;
    }

    if (videoRef.current) {
      videoRef.current.srcObject = null;
    }

    if (leaseRef.current) {
      leaseRef.current = null;
    }
    setActiveLeaseId(null);

    setIsSessionActive(false);
    setStatusText("Не запущена");
    setStreamStage("idle");
  }, []);

  const stopSessionRef = useRef(stopSession);
  stopSessionRef.current = stopSession;

  // Загрузка инвентаря устройств и состояния стрима
  const fetchDeviceInfo = useCallback(
    async (deviceId: number, refresh = false) => {
      setLoadingInventory(true);
      try {
        const [inv, stateRes, ctlStat] = await Promise.all([
          getDeviceInventory(deviceId, refresh).catch(async () => {
            if (refresh) {
              return getDeviceInventory(deviceId, false).catch(() => ({ displays: [], cameras: [] }));
            }
            return { displays: [], cameras: [] };
          }),
          getDeviceStreamState(deviceId).catch(() => null),
          getControlStatus(deviceId).catch(() => null),
        ]);

        const displays: DisplaySource[] = (inv?.displays || []).filter(
          (d: DisplaySource) => d.policy !== "denied"
        );
        const cameras: CameraSource[] = inv?.cameras || [];

        // Fallback если дисплей не вернулся, но агент сообщает screen
        if (displays.length === 0 && ctlStat?.agent?.screen) {
          const sc = ctlStat.agent.screen;
          const w = sc.virtual_width || 1920;
          const h = sc.virtual_height || 1080;
          displays.push({
            id: "0",
            name: "Основной экран",
            resolution: `${w}×${h}`,
            width: w,
            height: h,
            is_primary: true,
            primary: true,
            policy: "input",
          });
        }

        const normalizedInv: DeviceInventory = { displays, cameras };
        setInventory(normalizedInv);

        if (ctlStat?.agent) {
          rcRef.current.setPresence(ctlStat.agent);
        }

        const streamInfo = stateRes?.stream || (ctlStat?.agent as any)?.stream || null;
        setActiveStream(streamInfo);

        if (streamInfo?.state === "running") {
          setStreamStage("running");
        } else if (streamInfo?.state === "starting") {
          setStreamStage("starting");
        } else if (streamInfo?.state === "stopping") {
          setStreamStage("stopping");
        } else {
          setStreamStage("idle");
        }

        // Выбор источника по умолчанию
        if (streamInfo?.source_id && streamInfo?.mode) {
          setSelectedSourceKey(`${streamInfo.mode}:${streamInfo.source_id}`);
        } else if (displays.length > 0) {
          const primary = displays.find((d: DisplaySource) => d.is_primary || d.primary) || displays[0];
          setSelectedSourceKey(`desktop:${primary.id || primary.desktop_id || "0"}`);
        } else if (cameras.length > 0) {
          setSelectedSourceKey(`usb-camera:${cameras[0].id || cameras[0].camera_id || "0"}`);
        }
      } catch (err: any) {
        console.warn("Failed to fetch device inventory/state", err);
      } finally {
        setLoadingInventory(false);
      }
    },
    []
  );

  // Смена выбранного терминала
  const handleSelectDevice = async (device: DeviceListItem) => {
    if (selectedDevice?.device_id === device.device_id) return;
    setBannerError(null);
    setRefusalNotice(null);
    await stopSession();
    setSelectedDevice(device);
    setStatusText("Не запущена");
    await fetchDeviceInfo(device.device_id);

    // Автоматическое подключение для зрителя (viewer)
    if (isViewer && canView && device.status === "online") {
      void handleViewerConnect(device.device_id);
    }
  };

  // Подключение зрителя (Viewer)
  const handleViewerConnect = useCallback(
    async (deviceId: number) => {
      setBannerError(null);
      setRefusalNotice(null);
      setStreamStage("starting");
      setStatusText("Запрос аренды для просмотра...");

      try {
        const leaseRes = await acquireControlLease(deviceId, "view");
        setActiveLeaseId(leaseRes.lease_id);
        leaseRef.current = { id: leaseRes.lease_id, deviceId };
        coordinatorRef.current.startSession({
          deviceId,
          leaseId: leaseRes.lease_id,
          streamInstanceId: "viewer",
          keepaliveIntervalMs: (leaseRes.keepalive_sec ? Math.min(leaseRes.keepalive_sec / 2, 5) : 5) * 1000,
        });

        setStatusText("Подключение к медиапотоку...");
        const sessionData = await createVideoSession(deviceId);
        const wsUrl = getJanusWsUrl(sessionData.janus_ws);

        const client = new JanusStreamingClient({
          wsUrl,
          mountpointId: sessionData.mountpoint_id,
          pin: sessionData.pin,
          onRemoteTrack: (stream) => {
            if (videoRef.current) {
              videoRef.current.srcObject = stream;
              videoRef.current.play().catch(() => {});
            }
          },
          onStatusChange: (status) => {
            if (status === "connecting") {
              setStatusText("Соединение с медиасервером...");
            } else if (status === "streaming" || status === "webrtcup") {
              setStatusText("В эфире");
              setStreamStage("running");
              setIsSessionActive(true);
            }
          },
          onError: (err) => {
            const errMsg = typeof err === "string" ? err : err.message;
            message.error(errMsg);
            setStatusText(`Ошибка медиасервера: ${errMsg}`);
          },
        });

        await client.start();
        janusClientRef.current = client;
        setIsSessionActive(true);
        setStreamStage("running");
        setStatusText("В эфире");
      } catch (err: any) {
        const formatted = formatVideoError(err);
        setBannerError({ code: "viewer_connect_failed", message: formatted.message });
        message.error(formatted.message);
        setIsSessionActive(false);
        await stopSession();
        setStreamStage("failed");
        setStatusText(formatted.message);
      }
    },
    [stopSession]
  );

  // Запуск трансляции оператором
  const handleOperatorStart = async () => {
    if (!selectedDevice) return;
    if (selectedDevice.status !== "online") {
      message.warning("Терминал не на связи (offline). Запуск трансляции невозможен.");
      return;
    }

    setBannerError(null);
    setRefusalNotice(null);
    setStreamStage("starting");
    setStatusText("Запрос аренды терминала...");

    try {
      // 1. Оператор запрашивает аренду со scope 'stream'
      const leaseRes = await acquireControlLease(selectedDevice.device_id, "stream");
      setActiveLeaseId(leaseRes.lease_id);
      leaseRef.current = { id: leaseRes.lease_id, deviceId: selectedDevice.device_id };

      // 2. Route-before-Start: подготовка медиаканала (ingress и Janus mountpoint) ДО запуска FFmpeg
      setStatusText("Подготовка медиаканала (маршрутизация)...");
      const sessionData = await createVideoSession(selectedDevice.device_id);

      // 3. Определение режима и запуск видеопотока на терминале
      const colonIdx = selectedSourceKey.indexOf(":");
      const mode = colonIdx !== -1 ? selectedSourceKey.slice(0, colonIdx) : "desktop";
      const source_id = colonIdx !== -1 ? selectedSourceKey.slice(colonIdx + 1) : "0";
      setStatusText("Запуск видеопотока на терминале...");

      const startRes = await startDeviceStream(selectedDevice.device_id, {
        mode: mode as "desktop" | "usb-camera",
        source_id: source_id || "0",
        profile: selectedProfile,
        lease_id: leaseRes.lease_id,
      });

      // Запуск координатора сессии (generation tracking и неперекрывающийся keepalive)
      coordinatorRef.current.startSession({
        deviceId: selectedDevice.device_id,
        leaseId: leaseRes.lease_id,
        streamInstanceId: startRes.stream_instance_id,
        keepaliveIntervalMs: (leaseRes.keepalive_sec ? Math.min(leaseRes.keepalive_sec / 2, 5) : 5) * 1000,
      });

      // 4. Подключение WebRTC-клиента Janus
      setStatusText("Подключение к медиасерверу...");
      const wsUrl = getJanusWsUrl(sessionData.janus_ws);

      const client = new JanusStreamingClient({
        wsUrl,
        mountpointId: sessionData.mountpoint_id,
        pin: sessionData.pin,
        onRemoteTrack: (stream) => {
          if (videoRef.current) {
            videoRef.current.srcObject = stream;
            videoRef.current.play().catch(() => {});
          }
        },
        onStatusChange: (status) => {
          if (status === "connecting") {
            setStatusText("Соединение с медиасервером...");
          } else if (status === "streaming" || status === "webrtcup") {
            setStatusText("В эфире");
            setStreamStage("running");
            setIsSessionActive(true);
          }
        },
        onError: (err) => {
          const errMsg = typeof err === "string" ? err : err.message;
          message.error(errMsg);
          setStatusText(`Ошибка медиасервера: ${errMsg}`);
        },
      });

      await client.start();
      janusClientRef.current = client;

      // Успешный запуск
      setActiveStream({
        state: (startRes.state as any) || "running",
        mode,
        source_id,
        stream_instance_id: startRes.stream_instance_id,
      });
      setStreamStage("running");
      setIsSessionActive(true);
      setStatusText("В эфире");
      message.success("Трансляция успешно запущена");

      // 5. Периодический опрос качества и RTP пакетов
      prevPacketsRef.current = null;
      lastPacketGrowthTimeRef.current = Date.now();
      pollTimerRef.current = setInterval(async () => {
        try {
          const [stat, stateRes, ctlStat] = await Promise.all([
            getVideoSessionStatus(selectedDevice.device_id),
            getDeviceStreamState(selectedDevice.device_id).catch(() => null),
            getControlStatus(selectedDevice.device_id).catch(() => null),
          ]);

          if (ctlStat?.agent && rcRef.current.status !== "active") {
            rcRef.current.setPresence(ctlStat.agent);
          }
          if (stateRes?.stream) {
            coordinatorRef.current.handleStreamStateEvent(stateRes.stream);
            if (stateRes.stream.state !== "stopped" && stateRes.stream.state !== "failed") {
              setActiveStream(stateRes.stream);
            }
          }

          const now = Date.now();
          let pps = 0;
          if (prevPacketsRef.current) {
            const dt = (now - prevPacketsRef.current.time) / 1000;
            const dp = stat.rtp_packets - prevPacketsRef.current.packets;
            pps = dt > 0 ? Math.max(0, Math.round(dp / dt)) : 0;
          }
          prevPacketsRef.current = { packets: stat.rtp_packets, time: now };

          if (stat.streaming && stat.rtp_packets > 0) {
            if (pps === 0 && now - lastPacketGrowthTimeRef.current > 15000) {
              setStatusText("В эфире (кадры не поступают)");
            } else {
              if (pps > 0) {
                lastPacketGrowthTimeRef.current = now;
              }
              setStatusText(`В эфире (${pps} кадр/сек)`);
            }
          } else if (!stat.streaming) {
            setStatusText("Трансляция не передается");
          }
        } catch (e) {
          console.warn("Status poll error", e);
        }
      }, 5000);
    } catch (err: any) {
      const formatted = formatVideoError(err);
      setBannerError({ code: "start_failed", message: formatted.message });
      message.error(formatted.message);
      setActiveStream(null);
      setIsSessionActive(false);
      await stopSession("start_failed");
      setStreamStage("failed");
      setStatusText(formatted.message);
    }
  };

  // Остановка трансляции оператором
  const handleOperatorStop = async () => {
    if (!selectedDevice) return;
    setStreamStage("stopping");
    try {
      const curLease = coordinatorRef.current.session?.leaseId || activeLeaseId;
      if (curLease) {
        await stopDeviceStream(selectedDevice.device_id, curLease);
      }
    } catch {
      // ignore
    }
    await stopSession("operator_stop");
    message.info("Трансляция остановлена");
  };

  // Переключение режима удалённого управления
  const handleToggleControl = async () => {
    if (rc.status === "active") {
      await rc.disable("operator_manual");
      await coordinatorRef.current.detachInput();
      message.info("Удалённое управление отключено");
    } else {
      if (activeStream?.mode === "usb-camera") {
        message.warning("Управление мышью недоступно в режиме трансляции камеры");
        return;
      }
      if (activeStream?.mode && activeStream.mode !== "desktop") {
        message.warning("Управление мышью доступно только в подтверждённом режиме рабочего стола");
        return;
      }
      if (selectedProfile !== "low" && selectedProfile !== "480p") {
        message.warning("Удалённое управление разрешено только в режиме качества 480p (Эконом)");
        return;
      }
      try {
        await rc.enable(selectedProfile);
        message.success("Управление активировано");
      } catch (err: any) {
        // обработано в хуке
      }
    }
  };

  // Очистка при размонтировании
  useEffect(() => {
    const handleUnload = () => {
      const cur = leaseRef.current;
      if (cur) {
        const url = `/api/v1/video/devices/${cur.deviceId}/control/lease/${cur.id}`;
        navigator.sendBeacon?.(url);
      }
    };
    window.addEventListener("beforeunload", handleUnload);
    window.addEventListener("pagehide", handleUnload);

    return () => {
      window.removeEventListener("beforeunload", handleUnload);
      window.removeEventListener("pagehide", handleUnload);
      stopSessionRef.current();
    };
  }, []);

  // Человекочитаемое имя активного источника
  const activeSourceLabel = useMemo(() => {
    if (!activeStream || activeStream.state === "stopped" || (!activeStream.source_id && !activeStream.mode)) {
      return null;
    }
    const mode = activeStream.mode || "desktop";
    const srcId = String(activeStream.source_id ?? "");

    if (mode === "desktop" && inventory?.displays?.length) {
      const isGeneric = srcId === "0" || srcId === "disp" || srcId === "desktop" || srcId === "";
      const disp =
        inventory.displays.find(
          (d: DisplaySource) =>
            String(d.id) === srcId ||
            String(d.desktop_id) === srcId ||
            (isGeneric && (d.is_primary || d.primary))
        ) || (isGeneric ? inventory.displays[0] : null);

      if (disp) {
        const res = disp.resolution || (disp.width && disp.height ? `${disp.width}×${disp.height}` : null);
        const isPrimary = disp.is_primary ?? disp.primary;
        let name = disp.name?.replace(/desktop\s*#?\d*/gi, "").replace(/#\d+/g, "").trim();
        const label = isPrimary ? (name ? `Основной экран (${name})` : "Основной экран") : (name || "Монитор");
        return `${label}${res ? ` (${res})` : ""}`;
      }
    }

    if (mode === "usb-camera" && inventory?.cameras?.length) {
      const isGeneric = srcId === "0" || srcId === "cam" || srcId === "camera" || srcId === "";
      const cam =
        inventory.cameras.find(
          (c: CameraSource) =>
            String(c.id) === srcId ||
            String(c.camera_id) === srcId
        ) || (isGeneric ? inventory.cameras[0] : null);

      if (cam && cam.name) {
        return cam.name;
      }
    }

    if (mode === "desktop") {
      return "Рабочий стол (основной экран)";
    }
    if (mode === "usb-camera") {
      return "Камера терминала";
    }

    return "Источник видео";
  }, [activeStream, inventory]);

  const hasSources = (inventory?.displays?.length ?? 0) > 0 || (inventory?.cameras?.length ?? 0) > 0;
  const isCameraMode = activeStream?.mode === "usb-camera";

  return (
    <div style={{ padding: isMobile ? "0 12px 16px" : "0 24px 24px" }}>
      <PageHeader
        title="Видеонаблюдение"
        subtitle="Просмотр видеотрансляций с терминалов и удаленное управление"
      />

      {/* Основная адаптивная раскладка */}
      <div
        style={{
          display: "flex",
          gap: 16,
          alignItems: "flex-start",
          marginTop: 12,
        }}
      >
        {/* Левая панель списка терминалов (видна на Desktop) */}
        {isDesktop && (
          <div
            style={{
              width: 320,
              flex: "0 0 320px",
              position: "sticky",
              top: 72,
              maxHeight: "calc(100vh - 90px)",
            }}
          >
            <Card
              size="small"
              styles={{ body: { padding: 12 } }}
              style={{
                borderRadius: 8,
                border: `1px solid ${token.colorBorderSecondary}`,
                height: "100%",
              }}
            >
              <TerminalList
                devices={devices}
                terminalAddresses={terminalAddresses}
                selectedDevice={selectedDevice}
                onSelectDevice={handleSelectDevice}
                loading={loadingDevices}
                onRefresh={loadDevices}
                maxHeight="calc(100vh - 240px)"
              />
            </Card>
          </div>
        )}

        {/* Выдвижной Drawer со списком терминалов для мобильных и планшетов */}
        <Drawer
          title="Выбор терминала"
          placement="left"
          open={isDeviceDrawerOpen}
          onClose={() => setIsDeviceDrawerOpen(false)}
          width={screens.xs ? "85%" : 360}
          styles={{ body: { padding: 12 } }}
        >
          <TerminalList
            devices={devices}
            terminalAddresses={terminalAddresses}
            selectedDevice={selectedDevice}
            onSelectDevice={(dev) => {
              void handleSelectDevice(dev);
              setIsDeviceDrawerOpen(false);
            }}
            loading={loadingDevices}
            onRefresh={loadDevices}
            maxHeight="calc(100vh - 180px)"
          />
        </Drawer>

        {/* Правая / Основная рабочая зона */}
        <div
          style={{
            flex: 1,
            minWidth: 0,
            display: "flex",
            flexDirection: "column",
            gap: 14,
          }}
        >
          {selectedDevice ? (
            <>
              {/* 1. Контекст выбранного терминала */}
              <TerminalHeader
                selectedDevice={selectedDevice}
                address={terminalAddresses[selectedDevice.device_id]}
                onOpenDeviceDrawer={() => setIsDeviceDrawerOpen(true)}
                onRefreshDevice={() => fetchDeviceInfo(selectedDevice.device_id, true)}
                loadingRefresh={loadingInventory}
                isMobile={isMobile}
              />

              {/* Уведомления об ошибках или конфликте аренды */}
              {bannerError && (
                <Alert
                  type="warning"
                  showIcon
                  message={bannerError.message}
                  closable
                  onClose={() => setBannerError(null)}
                />
              )}

              {refusalNotice && (
                <Alert
                  type="error"
                  showIcon
                  message={refusalNotice}
                  description="Второй оператор не может перехватить управление до завершения текущего сеанса."
                  closable
                  onClose={() => setRefusalNotice(null)}
                />
              )}

              {/* 2. Главный видеоэкран с поддержкой 16:9 и всех состояний */}
              <VideoPlayerScreen
                selectedDevice={selectedDevice}
                videoRef={videoRef}
                isSessionActive={isSessionActive}
                streamStage={streamStage}
                errorMessage={bannerError?.message}
                isOperator={isOperator}
                isViewer={isViewer}
                activeSourceLabel={activeSourceLabel || undefined}
                isCameraMode={isCameraMode}
                streamMode={activeStream?.mode}
                onStartStream={isOperator ? handleOperatorStart : () => handleViewerConnect(selectedDevice.device_id)}
                onRetryStream={isOperator ? handleOperatorStart : () => handleViewerConnect(selectedDevice.device_id)}
                onRefreshTerminal={() => fetchDeviceInfo(selectedDevice.device_id, true)}
                rc={{
                  status: rc.status,
                  presence: rc.presence,
                  sendMove: rc.sendMove,
                  sendClick: rc.sendClick,
                  sendKey: rc.sendKey,
                  busyOwner: rc.busyOwner,
                }}
              />

              {/* 3. Панель управления трансляцией (Запустить / Остановить) */}
              <StreamControls
                isSessionActive={isSessionActive}
                streamStage={streamStage}
                isTerminalOnline={selectedDevice.status === "online"}
                isOperator={isOperator}
                isViewer={isViewer}
                onStart={handleOperatorStart}
                onStop={handleOperatorStop}
                hasSources={hasSources}
                activeSourceLabel={activeSourceLabel || undefined}
                isMobile={isMobile}
              />

              {/* 4. Выбор источника видео (экраны и камеры) для операторов */}
              {isOperator && (
                <SourceSelector
                  inventory={inventory}
                  selectedSourceKey={selectedSourceKey}
                  onSelectSourceKey={setSelectedSourceKey}
                  selectedProfile={selectedProfile}
                  onChangeProfile={setSelectedProfile}
                  loadingInventory={loadingInventory}
                  onRefreshInventory={() => fetchDeviceInfo(selectedDevice.device_id, true)}
                  disabled={isSessionActive}
                />
              )}

              {/* 5. Блок удалённого управления терминалом */}
              {isOperator && (
                <RemoteControlPanel
                  rcStatus={rc.status}
                  presence={rc.presence}
                  lease={rc.lease}
                  busyOwner={rc.busyOwner}
                  isSessionActive={isSessionActive}
                  isCameraMode={isCameraMode}
                  isTerminalOnline={selectedDevice.status === "online"}
                  selectedProfile={selectedProfile}
                  onEnableControl={handleToggleControl}
                  onDisableControl={handleToggleControl}
                  onSendShortcut={rc.sendShortcut}
                  onSendKey={rc.sendKey}
                  lastCommandResult={rc.lastClickResult}
                  isMobile={isMobile}
                />
              )}
            </>
          ) : (
            <Card style={{ textAlign: "center", padding: "64px 24px" }}>
              <Text type="secondary" style={{ fontSize: 15 }}>
                Выберите терминал в списке для начала видеонаблюдения
              </Text>
            </Card>
          )}
        </div>
      </div>
    </div>
  );
}
