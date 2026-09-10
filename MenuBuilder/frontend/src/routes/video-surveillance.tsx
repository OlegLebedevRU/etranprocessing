import React, { useEffect, useState, useRef, useCallback, useMemo } from "react";
import {
  Card,
  Input,
  Button,
  Badge,
  Tooltip,
  Typography,
  Space,
  Empty,
  Spin,
  Modal,
  Tag,
  Radio,
  Select,
  Alert,
  Divider,
  message,
  theme,
} from "antd";
import {
  SearchOutlined,
  PlayCircleOutlined,
  StopOutlined,
  VideoCameraOutlined,
  ReloadOutlined,
  ControlOutlined,
  DesktopOutlined,
  SwapOutlined,
  LockOutlined,
  InfoCircleOutlined,
} from "@ant-design/icons";
import { getDevices, DeviceListItem } from "../api/devices";
import {
  createVideoSession,
  getVideoSessionStatus,
  getJanusWsUrl,
  getControlStatus,
  getDeviceInventory,
  startDeviceStream,
  stopDeviceStream,
  getDeviceStreamState,
  acquireControlLease,
  releaseControlLease,
  keepaliveControlLease,
  changeControlScope,
  ClickResult,
  DeviceInventory,
  StreamPresenceInfo,
  DisplaySource,
  CameraSource,
} from "../api/video";
import { JanusStreamingClient } from "../api/janusClient";
import { useSession } from "../session/SessionContext";
import PageHeader from "../components/PageHeader";
import { useRemoteControl } from "../hooks/useRemoteControl";
import RemoteControlOverlay from "../components/RemoteControlOverlay";
import { hasPermission, PERMISSION_VIDEO_VIEW } from "../utils/permissions";

const { Text, Title } = Typography;

export default function VideoSurveillancePage() {
  const { user, loading: userLoading } = useSession();
  const { token } = theme.useToken();
  const orgId = typeof user?.org_id === "number" ? user.org_id : 1;

  // Role resolution per Prompt 3.2: Viewer (role 4) vs Operators (roles 1, 2, 3)
  const isViewer = user?.role_id === 4 || user?.role === "viewer";
  const canView = hasPermission(user, PERMISSION_VIDEO_VIEW);
  const isOperator =
    !isViewer &&
    Boolean(
      user?.role_id === 1 ||
        user?.role_id === 2 ||
        user?.role_id === 3 ||
        user?.is_superuser ||
        user?.role === "superuser" ||
        user?.role === "admin"
    );

  // Device list state
  const [devices, setDevices] = useState<DeviceListItem[]>([]);
  const [loadingDevices, setLoadingDevices] = useState<boolean>(true);
  const [search, setSearch] = useState<string>("");
  const [selectedDevice, setSelectedDevice] = useState<DeviceListItem | null>(null);

  // Inventory & Source selection state
  const [inventory, setInventory] = useState<DeviceInventory | null>(null);
  const [loadingInventory, setLoadingInventory] = useState<boolean>(false);
  const [selectedSourceKey, setSelectedSourceKey] = useState<string>("");
  const [selectedProfile, setSelectedProfile] = useState<string>("default");

  // Stream state & stage progression (stopping -> starting -> running)
  const [activeStream, setActiveStream] = useState<StreamPresenceInfo | null>(null);
  const [streamStage, setStreamStage] = useState<
    "idle" | "stopping" | "starting" | "running" | "failed"
  >("idle");
  const [bannerError, setBannerError] = useState<{
    code: string;
    message: string;
  } | null>(null);
  const [refusalNotice, setRefusalNotice] = useState<string | null>(null);

  // Media player & lease state
  const [isSessionActive, setIsSessionActive] = useState<boolean>(false);
  const [isStarting, setIsStarting] = useState<boolean>(false);
  const [statusText, setStatusText] = useState<string>("Ожидание запуска");
  const [activeLeaseId, setActiveLeaseId] = useState<string | null>(null);

  const videoRef = useRef<HTMLVideoElement | null>(null);
  const janusClientRef = useRef<JanusStreamingClient | null>(null);
  const pollTimerRef = useRef<any>(null);
  const keepaliveTimerRef = useRef<any>(null);
  const leaseRef = useRef<{ id: string; deviceId: number } | null>(null);
  const prevPacketsRef = useRef<{ packets: number; time: number } | null>(null);

  // Load devices list
  const loadDevices = useCallback(async () => {
    setLoadingDevices(true);
    try {
      const res = await getDevices(orgId, { page: 1, size: 100 });
      let allItems = res.items || [];
      if (res.pages > 1) {
        for (let p = 2; p <= res.pages; p++) {
          const nextRes = await getDevices(orgId, { page: p, size: 100 });
          allItems = allItems.concat(nextRes.items || []);
        }
      }
      setDevices(allItems);
      setSelectedDevice((prev) => {
        if (prev && allItems.some((d) => d.device_id === prev.device_id)) {
          return prev;
        }
        return allItems.length > 0 ? allItems[0] : null;
      });
    } catch (err: any) {
      message.error(err?.message || "Ошибка загрузки списка устройств");
    } finally {
      setLoadingDevices(false);
    }
  }, [orgId]);

  useEffect(() => {
    if (!userLoading) {
      loadDevices();
    }
  }, [loadDevices, userLoading]);

  // Filter devices
  const filteredDevices = useMemo(() => {
    if (!search.trim()) return devices;
    const q = search.trim().toLowerCase();
    return devices.filter(
      (d) =>
        String(d.device_id).includes(q) ||
        (d.sn && d.sn.toLowerCase().includes(q))
    );
  }, [devices, search]);

  const handleClickResult = useCallback((res: ClickResult) => {
    if (res.result === "injected") {
      const ms = res.latency_ms !== undefined ? ` (${res.latency_ms} мс)` : "";
      message.success(`Клик выполнен${ms}`);
    } else if (res.result === "unconfirmed") {
      message.warning("Клик не подтверждён — повторите вручную");
    } else if (res.result === "nack") {
      message.error(`Клик отклонён агентом: ${res.code || res.message || "ошибка"}`);
    }
  }, []);

  const handleErrorMessage = useCallback((msg: string) => {
    message.error(msg);
  }, []);

  const rc = useRemoteControl({
    deviceId: selectedDevice?.device_id ?? null,
    isSessionActive,
    onClickResult: handleClickResult,
    onErrorMessage: handleErrorMessage,
  });

  const rcRef = useRef(rc);
  rcRef.current = rc;

  // Clear keepalive interval
  const clearLeaseKeepalive = () => {
    if (keepaliveTimerRef.current) {
      clearInterval(keepaliveTimerRef.current);
      keepaliveTimerRef.current = null;
    }
  };

  // Start keepalive interval
  const startLeaseKeepalive = (deviceId: number, leaseId: string, keepaliveSec = 15) => {
    clearLeaseKeepalive();
    keepaliveTimerRef.current = setInterval(async () => {
      try {
        await keepaliveControlLease(deviceId, leaseId);
      } catch (e) {
        console.warn("Lease keepalive failed", e);
      }
    }, Math.max(5, keepaliveSec - 3) * 1000);
  };

  // Stop current active session cleanly
  const stopSession = useCallback(async () => {
    clearLeaseKeepalive();
    await rcRef.current.disable("session_stopped");

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
      const cur = leaseRef.current;
      leaseRef.current = null;
      setActiveLeaseId(null);
      try {
        await releaseControlLease(cur.deviceId, cur.id);
      } catch {
        // ignore
      }
    }

    setIsSessionActive(false);
    setIsStarting(false);
    setStatusText("Сессия остановлена");
    setStreamStage("idle");
  }, []);

  const stopSessionRef = useRef(stopSession);
  stopSessionRef.current = stopSession;

  // Load inventory and stream state for selected device
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

        const rawDisplays = (inv?.displays || []) as DisplaySource[];
        const rawCameras = (inv?.cameras || []) as CameraSource[];

        const displays: DisplaySource[] = rawDisplays.map((d: any) => ({
          ...d,
          id: String(d.id || d.desktop_id || "0"),
          desktop_id: String(d.desktop_id || d.id || "0"),
          is_primary: Boolean(d.is_primary ?? d.primary),
          primary: Boolean(d.primary ?? d.is_primary),
          resolution: d.resolution || (d.width && d.height ? `${d.width}x${d.height}` : undefined),
        }));

        const cameras: CameraSource[] = rawCameras.map((c: any) => ({
          ...c,
          id: String(c.id || c.camera_id || "0"),
          camera_id: String(c.camera_id || c.id || "0"),
        }));

        const agent = ctlStat?.agent;
        if (displays.length === 0 && (agent?.desktop_available || agent?.screen)) {
          const w = agent?.screen?.virtual_width || 1920;
          const h = agent?.screen?.virtual_height || 1080;
          displays.push({
            id: "0",
            desktop_id: "0",
            name: "Основной экран",
            resolution: `${w}x${h}`,
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

        // Set default selected source
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

  // Connect to stream (Viewer flow)
  const handleViewerConnect = useCallback(
    async (deviceId: number) => {
      setIsStarting(true);
      setBannerError(null);
      setRefusalNotice(null);
      setStatusText("Запрос аренды для просмотра...");

      try {
        // 1. Viewer acquires lease with scope 'view'
        const leaseRes = await acquireControlLease(deviceId, "view");
        setActiveLeaseId(leaseRes.lease_id);
        leaseRef.current = { id: leaseRes.lease_id, deviceId };
        startLeaseKeepalive(deviceId, leaseRes.lease_id, leaseRes.keepalive_sec);

        // 2. Init video session with pin
        setStatusText("Подключение к медиапотоку...");
        const sessionData = await createVideoSession(deviceId);
        const wsUrl = getJanusWsUrl(sessionData.janus_ws);

        // 3. Connect Janus streaming client with PIN
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
              setStatusText("Трансляция активна");
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
        setStatusText("Просмотр трансляции");
      } catch (err: any) {
        const detail = err?.response?.data?.detail;
        if (err?.response?.status === 409) {
          if (detail && typeof detail === "object" && detail.code === "stream_not_running") {
            setBannerError({
              code: "stream_not_running",
              message: "Трансляция не запущена оператором",
            });
            setStatusText("Трансляция не запущена");
          } else if (detail && typeof detail === "object" && detail.code === "lease_taken") {
            const owner = detail.owner_role
              ? `${detail.owner_role} (${detail.owner_masked || detail.owner_user_id || "..."})`
              : "другим пользователем";
            const exp = detail.expires_at
              ? ` до ${new Date(detail.expires_at).toLocaleTimeString()}`
              : "";
            setRefusalNotice(`Терминал занят: ${owner}${exp}`);
            setStatusText("Терминал занят");
          } else {
            const msg = typeof detail === "string" ? detail : JSON.stringify(detail);
            message.warning(`Отказ: ${msg}`);
            setStatusText(`Отказ: ${msg}`);
          }
        } else {
          const msg = err?.message || detail || "Ошибка подключения";
          message.error(msg);
          setStatusText(`Ошибка: ${msg}`);
        }
      } finally {
        setIsStarting(false);
      }
    },
    []
  );

  // Device selection change
  const handleSelectDevice = async (device: DeviceListItem) => {
    if (selectedDevice?.device_id === device.device_id) return;
    setBannerError(null);
    setRefusalNotice(null);
    await stopSession();
    setSelectedDevice(device);
    setStatusText("Ожидание запуска");
    await fetchDeviceInfo(device.device_id);

    // If role is viewer with permission, automatically attempt view connection
    if (isViewer && canView) {
      void handleViewerConnect(device.device_id);
    }
  };

  // Operator Start Stream
  const handleOperatorStart = async () => {
    if (!selectedDevice) return;
    setIsStarting(true);
    setBannerError(null);
    setRefusalNotice(null);
    setStreamStage("starting");
    setStatusText("Запрос аренды терминала...");

    try {
      // 1. Operator acquires lease with scope 'stream'
      const leaseRes = await acquireControlLease(selectedDevice.device_id, "stream");
      setActiveLeaseId(leaseRes.lease_id);
      leaseRef.current = { id: leaseRes.lease_id, deviceId: selectedDevice.device_id };
      startLeaseKeepalive(selectedDevice.device_id, leaseRes.lease_id, leaseRes.keepalive_sec);

      // 2. Parse selected mode and source_id
      const [mode, source_id] = selectedSourceKey.split(":");
      setStatusText("Запуск трансляции на терминале...");
      try {
        const startRes = await startDeviceStream(selectedDevice.device_id, {
          mode: mode as "desktop" | "usb-camera",
          source_id: source_id || "0",
          profile: selectedProfile,
          lease_id: leaseRes.lease_id,
        });

        setActiveStream({
          state: (startRes.state as any) || "running",
          mode,
          source_id,
          stream_instance_id: startRes.stream_instance_id,
        });
      } catch (streamErr: any) {
        console.warn("startDeviceStream warning/fallback", streamErr);
        setActiveStream({
          state: "running",
          mode,
          source_id,
        });
      }
      setStreamStage("running");

      // 3. Init WebRTC session and Janus mountpoint with PIN
      setStatusText("Подключение к медиасерверу...");
      const sessionData = await createVideoSession(selectedDevice.device_id);
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
            setStatusText("Трансляция активна");
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
      setStatusText("Трансляция запущена");

      // 4. Polling status
      prevPacketsRef.current = null;
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
            setActiveStream(stateRes.stream);
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
            setStatusText(`Идёт трансляция (${pps} pkt/s)`);
          }
        } catch (e) {
          console.warn("Status poll error", e);
        }
      }, 5000);
    } catch (err: any) {
      const detail = err?.response?.data?.detail;
      if (err?.response?.status === 409) {
        if (detail && typeof detail === "object" && detail.code === "lease_taken") {
          const owner = detail.owner_role
            ? `${detail.owner_role} (${detail.owner_masked || detail.owner_user_id || "..."})`
            : "другим пользователем";
          const exp = detail.expires_at
            ? ` до ${new Date(detail.expires_at).toLocaleTimeString()}`
            : "";
          setRefusalNotice(`Терминал занят: ${owner}${exp}`);
          setStatusText("Терминал занят");
        } else {
          const nackCode = detail?.code || detail?.nack?.code;
          if (nackCode) {
            handleBannerError(nackCode);
          } else {
            message.error(typeof detail === "string" ? detail : "Конфликт аренды");
          }
        }
      } else {
        const msg = err?.message || detail || "Ошибка запуска трансляции";
        message.error(msg);
        setStatusText(`Ошибка: ${msg}`);
      }
      setStreamStage("failed");
      await stopSession();
    } finally {
      setIsStarting(false);
    }
  };

  // Helper for nack banner errors
  const handleBannerError = (code: string) => {
    const errorMap: Record<string, string> = {
      source_unavailable: "Выбранный источник видео недоступен на терминале",
      session_unavailable: "Сессия рабочего стола пользователя недоступна",
      failed: "Сбой запуска видеозахвата ffmpeg на терминале",
      ffmpeg_missing: "Утилита ffmpeg не найдена на терминале",
      terminal_timeout: "Таймаут ответа терминала на команду трансляции",
      busy_transition: "Терминал занят переключением видеопотока",
    };
    const msg = errorMap[code] || `Ошибка терминала: ${code}`;
    setBannerError({ code, message: msg });
  };

  // Operator Switch Source
  const handleOperatorSwitch = async () => {
    if (!selectedDevice || !leaseRef.current) return;
    setIsStarting(true);
    setBannerError(null);
    setStreamStage("stopping");
    setStatusText("Остановка текущего источника...");

    try {
      const [mode, source_id] = selectedSourceKey.split(":");
      setStreamStage("starting");
      setStatusText(`Переключение на ${mode === "desktop" ? "экран" : "камеру"} ${source_id}...`);

      const res = await startDeviceStream(selectedDevice.device_id, {
        mode: mode as "desktop" | "usb-camera",
        source_id: source_id || "0",
        profile: selectedProfile,
        lease_id: leaseRef.current.id,
      });

      setActiveStream({
        state: (res.state as any) || "running",
        mode,
        source_id,
        stream_instance_id: res.stream_instance_id,
      });
      setStreamStage("running");
      setStatusText("Источник успешно переключен");
      message.success("Источник трансляции переключен");
    } catch (err: any) {
      const detail = err?.response?.data?.detail;
      const nackCode = detail?.code || detail?.nack?.code;
      if (nackCode) {
        handleBannerError(nackCode);
      } else {
        message.error(typeof detail === "string" ? detail : "Ошибка переключения источника");
      }
      setStreamStage("failed");
    } finally {
      setIsStarting(false);
    }
  };

  // Operator Stop Stream
  const handleOperatorStop = async () => {
    if (!selectedDevice) return;
    try {
      if (leaseRef.current) {
        await stopDeviceStream(selectedDevice.device_id, leaseRef.current.id);
      }
    } catch {
      // ignore
    }
    await stopSession();
    message.info("Трансляция остановлена");
  };

  // Page unload and cleanup effects
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

  const shortenSn = (sn: string) => {
    if (!sn) return "—";
    if (sn.length <= 12) return sn;
    return `${sn.slice(0, 6)}...${sn.slice(-4)}`;
  };

  const isCurrentSourceActive = useMemo(() => {
    if (!activeStream || !selectedSourceKey) return false;
    const [mode, source_id] = selectedSourceKey.split(":");
    return activeStream.mode === mode && String(activeStream.source_id) === String(source_id);
  }, [activeStream, selectedSourceKey]);

  return (
    <div style={{ padding: "0 24px 24px" }}>
      <PageHeader
        title="Видеонаблюдение"
        subtitle="Просмотр WebRTC трансляций с терминалов и удаленное управление"
      />

      <div
        style={{
          display: "flex",
          gap: 16,
          alignItems: "stretch",
          minHeight: "calc(100vh - 180px)",
        }}
      >
        {/* Left Column: Device list (~280px) */}
        <Card
          title={
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between" }}>
              <span>Устройства ({filteredDevices.length})</span>
              <Button
                type="text"
                size="small"
                icon={<ReloadOutlined />}
                onClick={loadDevices}
              />
            </div>
          }
          style={{ width: 280, flexShrink: 0, display: "flex", flexDirection: "column" }}
          bodyStyle={{ padding: 12, display: "flex", flexDirection: "column", flex: 1 }}
        >
          <Input
            prefix={<SearchOutlined style={{ color: token.colorTextPlaceholder }} />}
            placeholder="Поиск по ID или SN..."
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            allowClear
            style={{ marginBottom: 12 }}
          />

          <div style={{ flex: 1, overflowY: "auto", maxHeight: "calc(100vh - 280px)" }}>
            {loadingDevices ? (
              <div style={{ textAlign: "center", padding: 32 }}>
                <Spin />
              </div>
            ) : filteredDevices.length === 0 ? (
              <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Нет устройств" />
            ) : (
              filteredDevices.map((dev) => {
                const isSelected = selectedDevice?.device_id === dev.device_id;
                const isOnline = dev.status === "online";
                return (
                  <div
                    key={dev.device_id}
                    onClick={() => handleSelectDevice(dev)}
                    style={{
                      padding: "10px 12px",
                      marginBottom: 6,
                      borderRadius: 6,
                      cursor: "pointer",
                      backgroundColor: isSelected
                        ? token.colorPrimaryBg
                        : token.colorBgContainer,
                      border: `1px solid ${
                        isSelected ? token.colorPrimaryBorder : token.colorBorderSecondary
                      }`,
                      transition: "all 0.2s ease",
                    }}
                  >
                    <div
                      style={{
                        display: "flex",
                        justifyContent: "space-between",
                        alignItems: "center",
                      }}
                    >
                      <Text strong style={{ fontSize: 14 }}>
                        #{dev.device_id}
                      </Text>
                      <Badge
                        status={isOnline ? "success" : "default"}
                        text={
                          <span style={{ fontSize: 12, color: token.colorTextSecondary }}>
                            {dev.status}
                          </span>
                        }
                      />
                    </div>
                    <div style={{ marginTop: 4 }}>
                      <Tooltip title={`Серийный номер: ${dev.sn}`}>
                        <Text type="secondary" style={{ fontSize: 12, fontFamily: "monospace" }}>
                          {shortenSn(dev.sn)}
                        </Text>
                      </Tooltip>
                    </div>
                  </div>
                );
              })
            )}
          </div>
        </Card>

        {/* Right Column: Player & Controls Panel */}
        <Card
          style={{ flex: 1, display: "flex", flexDirection: "column" }}
          bodyStyle={{ padding: 24, display: "flex", flexDirection: "column", flex: 1 }}
        >
          {selectedDevice ? (
            <>
              {/* Header */}
              <div
                style={{
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                  marginBottom: 16,
                }}
              >
                <div>
                  <Title level={4} style={{ margin: 0 }}>
                    Устройство #{selectedDevice.device_id}
                  </Title>
                  <Text type="secondary" style={{ fontSize: 13, fontFamily: "monospace" }}>
                    SN: {selectedDevice.sn}
                  </Text>
                  {isViewer && (
                    <Tag color="purple" style={{ marginLeft: 8 }}>
                      Режим наблюдателя (только просмотр)
                    </Tag>
                  )}
                </div>

                {/* Operator Actions Bar */}
                {isOperator && (
                  <Space>
                    {!isSessionActive ? (
                      <Button
                        type="primary"
                        icon={<PlayCircleOutlined />}
                        onClick={handleOperatorStart}
                        loading={isStarting}
                        disabled={!selectedSourceKey}
                      >
                        Запустить
                      </Button>
                    ) : (
                      <>
                        <Button
                          icon={<SwapOutlined />}
                          onClick={handleOperatorSwitch}
                          loading={isStarting && streamStage === "starting"}
                          disabled={isCurrentSourceActive || isStarting}
                        >
                          Переключить
                        </Button>

                        {/* Remote Control Toggle */}
                        {activeStream?.mode === "desktop" ? (
                          rc.status === "active" ? (
                            <Button
                              danger
                              type="primary"
                              icon={<ControlOutlined />}
                              onClick={async () => {
                                await rc.disable("user_toggle");
                                if (leaseRef.current) {
                                  await changeControlScope(selectedDevice.device_id, "stream").catch(() => {});
                                }
                                message.info("Управление отключено");
                              }}
                            >
                              Отключить управление
                            </Button>
                          ) : (
                            <Button
                              icon={<ControlOutlined />}
                              disabled={
                                !rc.presence?.online ||
                                !rc.presence?.desktop_available ||
                                rc.status === "acquiring" ||
                                isStarting
                              }
                              loading={rc.status === "acquiring"}
                              onClick={() => {
                                if (!rc.presence?.online) {
                                  message.warning("Управление недоступно: агент offline");
                                  return;
                                }
                                if (!rc.presence?.desktop_available) {
                                  message.warning("Экран терминала заблокирован");
                                  return;
                                }
                                Modal.confirm({
                                  title: "Включение удалённого управления",
                                  content:
                                    "Вы управляете клавиатурой и мышью терминала. Действия подтверждаются агентом и журналируются.",
                                  okText: "Включить",
                                  cancelText: "Отмена",
                                  onOk: async () => {
                                    try {
                                      await changeControlScope(selectedDevice.device_id, "input");
                                      await rc.enable();
                                    } catch (e: any) {
                                      message.error(e?.response?.data?.detail || "Ошибка включения управления");
                                    }
                                  },
                                });
                              }}
                            >
                              Включить управление
                            </Button>
                          )
                        ) : (
                          <Tooltip title="Управление вводом доступно только в режиме трансляции рабочего стола">
                            <Button icon={<ControlOutlined />} disabled>
                              Управление (недоступно для камеры)
                            </Button>
                          </Tooltip>
                        )}

                        <Button danger icon={<StopOutlined />} onClick={handleOperatorStop}>
                          Остановить
                        </Button>
                      </>
                    )}
                  </Space>
                )}
              </div>

              {/* Source Selection & Settings Panel (Operators only) */}
              {isOperator && (
                <div
                  style={{
                    marginBottom: 16,
                    padding: "12px 16px",
                    borderRadius: 8,
                    backgroundColor: token.colorFillAlter,
                    border: `1px solid ${token.colorBorderSecondary}`,
                  }}
                >
                  <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 8 }}>
                    <Text strong style={{ fontSize: 13 }}>
                      Выбор источника трансляции:
                    </Text>
                    <Space size="middle">
                      <Space size="small">
                        <Text type="secondary" style={{ fontSize: 12 }}>
                          Профиль качества:
                        </Text>
                        <Select
                          size="small"
                          value={selectedProfile}
                          onChange={setSelectedProfile}
                          style={{ width: 110 }}
                          options={[
                            { label: "Default (720p)", value: "default" },
                            { label: "Low (480p)", value: "low" },
                          ]}
                          disabled={isSessionActive && isCurrentSourceActive}
                        />
                      </Space>
                      <Button
                        size="small"
                        type="link"
                        icon={<ReloadOutlined />}
                        loading={loadingInventory}
                        onClick={() => fetchDeviceInfo(selectedDevice.device_id, true)}
                      >
                        Обновить источники
                      </Button>
                    </Space>
                  </div>

                  {loadingInventory ? (
                    <div style={{ textAlign: "center", padding: 8 }}>
                      <Spin size="small" />
                    </div>
                  ) : (
                    <Radio.Group
                      value={selectedSourceKey}
                      onChange={(e) => setSelectedSourceKey(e.target.value)}
                      style={{ width: "100%" }}
                    >
                      <div style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                        {/* Displays */}
                        {inventory?.displays && inventory.displays.length > 0 && (
                          <div>
                            <Text type="secondary" style={{ fontSize: 11, fontWeight: 600 }}>
                              ДИСПЛЕИ:
                            </Text>
                            <div style={{ display: "flex", flexWrap: "wrap", gap: 12, marginTop: 4 }}>
                              {inventory.displays.map((disp: DisplaySource) => {
                                const dispId = disp.id || disp.desktop_id || "0";
                                const isPrimary = disp.is_primary ?? disp.primary;
                                const res = disp.resolution || (disp.width && disp.height ? `${disp.width}x${disp.height}` : null);
                                return (
                                  <Radio key={`desktop:${dispId}`} value={`desktop:${dispId}`}>
                                    <Space size="small">
                                      <DesktopOutlined />
                                      <span>{disp.name}</span>
                                      {res && <Tag style={{ fontSize: 11 }}>{res}</Tag>}
                                      {isPrimary && <Tag color="blue" style={{ fontSize: 11 }}>Primary</Tag>}
                                      {disp.policy && (
                                        <Tag color={disp.policy === "denied" ? "red" : "default"} style={{ fontSize: 11 }}>
                                          {disp.policy}
                                        </Tag>
                                      )}
                                    </Space>
                                  </Radio>
                                );
                              })}
                            </div>
                          </div>
                        )}

                        {/* Cameras */}
                        {inventory?.cameras && inventory.cameras.length > 0 && (
                          <div style={{ marginTop: inventory?.displays?.length ? 4 : 0 }}>
                            <Text type="secondary" style={{ fontSize: 11, fontWeight: 600 }}>
                              КАМЕРЫ:
                            </Text>
                            <div style={{ display: "flex", flexWrap: "wrap", gap: 12, marginTop: 4 }}>
                              {inventory.cameras.map((cam: CameraSource) => {
                                const camId = cam.id || cam.camera_id || "0";
                                return (
                                  <Radio key={`usb-camera:${camId}`} value={`usb-camera:${camId}`}>
                                    <Space size="small">
                                      <VideoCameraOutlined />
                                      <span>{cam.name}</span>
                                      <Tag color={cam.available ? "green" : "default"} style={{ fontSize: 11 }}>
                                        {cam.available ? "Доступна" : "Недоступна"}
                                      </Tag>
                                    </Space>
                                  </Radio>
                                );
                              })}
                            </div>
                          </div>
                        )}

                        {(!inventory?.displays?.length && !inventory?.cameras?.length) && (
                          <Text type="secondary" style={{ fontSize: 12 }}>
                            Источники видео не обнаружены в присутствии терминала.
                          </Text>
                        )}
                      </div>
                    </Radio.Group>
                  )}
                </div>
              )}

              {/* Banners & Notifications */}
              {bannerError && (
                <Alert
                  type="warning"
                  showIcon
                  message={bannerError.message}
                  description={`Код терминала: ${bannerError.code}`}
                  closable
                  onClose={() => setBannerError(null)}
                  style={{ marginBottom: 16 }}
                />
              )}

              {refusalNotice && (
                <Alert
                  type="error"
                  showIcon
                  icon={<LockOutlined />}
                  message={refusalNotice}
                  description="Второй пользователь не может перехватить сессию до окончания текущей аренды."
                  closable
                  onClose={() => setRefusalNotice(null)}
                  style={{ marginBottom: 16 }}
                />
              )}

              {/* Video Area */}
              <div
                style={{
                  position: "relative",
                  width: "100%",
                  maxHeight: "560px",
                  height: "560px",
                  backgroundColor: "#000000",
                  borderRadius: 8,
                  overflow: "hidden",
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                }}
              >
                <video
                  ref={videoRef}
                  autoPlay
                  playsInline
                  muted
                  controls={false}
                  style={{
                    width: "100%",
                    height: "100%",
                    objectFit: "contain",
                    display: isSessionActive ? "block" : "none",
                  }}
                />

                <RemoteControlOverlay
                  videoRef={videoRef}
                  active={isSessionActive && rc.status === "active"}
                  presence={rc.presence}
                  sendMove={rc.sendMove}
                  sendClick={rc.sendClick}
                  sendKey={rc.sendKey}
                  isCameraMode={activeStream?.mode === "usb-camera"}
                />

                {!isSessionActive && (
                  <div style={{ textAlign: "center", color: "rgba(255,255,255,0.45)" }}>
                    <VideoCameraOutlined style={{ fontSize: 56, marginBottom: 16 }} />
                    <div style={{ fontSize: 16 }}>Трансляция не запущена</div>
                    <div style={{ fontSize: 13, marginTop: 4 }}>
                      {isViewer
                        ? "Ожидание запуска трансляции оператором терминала"
                        : "Выберите источник и нажмите «Запустить» для начала трансляции"}
                    </div>
                  </div>
                )}
              </div>

              {/* Status Line */}
              <div
                style={{
                  marginTop: 16,
                  padding: "10px 16px",
                  borderRadius: 6,
                  backgroundColor: token.colorFillAlter,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "space-between",
                }}
              >
                <Space wrap>
                  <Badge
                    status={
                      streamStage === "running"
                        ? "success"
                        : streamStage === "starting"
                          ? "processing"
                          : streamStage === "stopping"
                            ? "warning"
                            : streamStage === "failed"
                              ? "error"
                              : "default"
                    }
                  />
                  <Text strong>Статус:</Text>
                  <Text>{statusText}</Text>
                  {streamStage !== "idle" && (
                    <Tag
                      color={
                        streamStage === "running"
                          ? "green"
                          : streamStage === "starting"
                            ? "blue"
                            : streamStage === "stopping"
                              ? "orange"
                              : "red"
                      }
                    >
                      Этап: {streamStage}
                    </Tag>
                  )}
                  {activeStream && (
                    <Tag>
                      Источник: {activeStream.mode || "desktop"} #{activeStream.source_id || "0"}
                      {activeStream.restart_count ? ` (рестартов: ${activeStream.restart_count})` : ""}
                    </Tag>
                  )}
                  {rc.presence && (
                    <>
                      <Tag color={rc.presence.online ? (rc.presence.stale ? "orange" : "green") : "default"}>
                        Агент: {rc.presence.online ? (rc.presence.stale ? "stale" : "online") : "offline"}
                      </Tag>
                      <Tag color={rc.presence.desktop_available ? "green" : "orange"}>
                        {rc.presence.desktop_available ? "Экран доступен" : "Экран заблокирован"}
                      </Tag>
                    </>
                  )}
                  {rc.status === "active" && (
                    <Tag color="blue">
                      Управление активно {rc.lease?.expires_at ? `до ${new Date(rc.lease.expires_at).toLocaleTimeString()}` : ""}
                    </Tag>
                  )}
                  {rc.status === "busy" && (
                    <Tag color="volcano">
                      Занято другим оператором {rc.busyOwner ? `(#${rc.busyOwner})` : ""}
                    </Tag>
                  )}
                </Space>

                {isSessionActive && (
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    Mountpoint ID: {selectedDevice.device_id}
                  </Text>
                )}
              </div>
            </>
          ) : (
            <div
              style={{
                display: "flex",
                flex: 1,
                alignItems: "center",
                justifyContent: "center",
              }}
            >
              <Empty description="Выберите устройство в списке слева для начала видеонаблюдения" />
            </div>
          )}
        </Card>
      </div>
    </div>
  );
}
