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
} from "@ant-design/icons";
import { getDevices, DeviceListItem } from "../api/devices";
import {
  createVideoSession,
  getVideoSessionStatus,
  getJanusWsUrl,
  getControlStatus,
  ClickResult,
} from "../api/video";
import { JanusStreamingClient } from "../api/janusClient";
import { useSession } from "../session/SessionContext";
import PageHeader from "../components/PageHeader";
import { useRemoteControl } from "../hooks/useRemoteControl";
import RemoteControlOverlay from "../components/RemoteControlOverlay";

const { Text, Title } = Typography;

export default function VideoSurveillancePage() {
  const { user, loading: userLoading } = useSession();
  const { token } = theme.useToken();
  const orgId = typeof user?.org_id === "number" ? user.org_id : 1;

  const [devices, setDevices] = useState<DeviceListItem[]>([]);
  const [loadingDevices, setLoadingDevices] = useState<boolean>(true);
  const [search, setSearch] = useState<string>("");
  const [selectedDevice, setSelectedDevice] = useState<DeviceListItem | null>(null);

  const [isSessionActive, setIsSessionActive] = useState<boolean>(false);
  const [isStarting, setIsStarting] = useState<boolean>(false);
  const [statusText, setStatusText] = useState<string>("Ожидание запуска");

  const videoRef = useRef<HTMLVideoElement | null>(null);
  const janusClientRef = useRef<JanusStreamingClient | null>(null);
  const pollTimerRef = useRef<any>(null);
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

  const isOperatorRole = useMemo(() => {
    if (!user) return false;
    if (user.role_id === 4 || user.role === "viewer") return false;
    return (
      user.role_id === 1 ||
      user.role_id === 2 ||
      user.role_id === 3 ||
      user.role === "superuser" ||
      user.role === "admin" ||
      user.role === "user" ||
      Boolean(user.is_superuser)
    );
  }, [user]);

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

  // Fetch initial control status whenever selected device changes
  useEffect(() => {
    if (!selectedDevice) return;
    let isMounted = true;
    getControlStatus(selectedDevice.device_id)
      .then((stat) => {
        if (isMounted && stat?.agent) {
          rcRef.current.setPresence(stat.agent);
        }
      })
      .catch(() => {});
    return () => {
      isMounted = false;
    };
  }, [selectedDevice?.device_id]);

  // Stop current active session cleanly
  const stopSession = useCallback(async () => {
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

    setIsSessionActive(false);
    setIsStarting(false);
    setStatusText("Сессия остановлена");
  }, []);

  const stopSessionRef = useRef(stopSession);
  stopSessionRef.current = stopSession;

  // Clean up on component unmount
  useEffect(() => {
    return () => {
      stopSessionRef.current();
    };
  }, []);

  // Change selected device
  const handleSelectDevice = async (device: DeviceListItem) => {
    if (selectedDevice?.device_id === device.device_id) return;
    await rcRef.current.disable("device_changed");
    if (isSessionActive || isStarting) {
      await stopSession();
    }
    setSelectedDevice(device);
    setStatusText("Ожидание запуска");
  };

  // Start video session
  const handleStart = async () => {
    if (!selectedDevice) return;

    await rcRef.current.disable("session_start_clean");

    setIsStarting(true);
    setStatusText("Инициализация видеосессии...");

    try {
      // 1. Backend session init (ensures ingress route and Janus mountpoint)
      const sessionData = await createVideoSession(selectedDevice.device_id);
      const wsUrl = getJanusWsUrl(sessionData.janus_ws);

      setStatusText("Соединение с медиасервером...");

      // 2. Connect to Janus via WebSocket
      const client = new JanusStreamingClient({
        wsUrl,
        mountpointId: sessionData.mountpoint_id,
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
            setStatusText("Медиасервер подключен (ожидание видео)");
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
      setStatusText("Ожидание потока от устройства (запустите трансляцию)");

      // 3. Status polling every 5 seconds
      prevPacketsRef.current = null;
      pollTimerRef.current = setInterval(async () => {
        try {
          const [stat, ctlStat] = await Promise.all([
            getVideoSessionStatus(selectedDevice.device_id),
            getControlStatus(selectedDevice.device_id).catch(() => null),
          ]);

          if (ctlStat?.agent && rcRef.current.status !== "active") {
            rcRef.current.setPresence(ctlStat.agent);
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
          } else {
            setStatusText("Ожидание потока от устройства (запустите трансляцию)");
          }
        } catch (err: any) {
          console.warn("Status poll error:", err);
        }
      }, 5000);
    } catch (err: any) {
      const msg = err?.message || "Не удалось запустить видеосессию";
      message.error(msg);
      setStatusText(`Ошибка: ${msg}`);
      await stopSession();
    } finally {
      setIsStarting(false);
    }
  };

  const shortenSn = (sn: string) => {
    if (!sn) return "—";
    if (sn.length <= 12) return sn;
    return `${sn.slice(0, 6)}...${sn.slice(-4)}`;
  };

  return (
    <div style={{ padding: "0 24px 24px" }}>
      <PageHeader
        title="Видеонаблюдение"
        subtitle="Просмотр WebRTC трансляций с терминалов в реальном времени"
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
          bodyStyle={{ padding: "12px", flex: 1, display: "flex", flexDirection: "column" }}
        >
          <Input
            placeholder="Поиск по ID / SN"
            prefix={<SearchOutlined style={{ color: token.colorTextPlaceholder }} />}
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

        {/* Right Column: Player Panel */}
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
                    Устройство {selectedDevice.device_id}
                  </Title>
                  <Text type="secondary" style={{ fontSize: 13, fontFamily: "monospace" }}>
                    SN: {selectedDevice.sn}
                  </Text>
                </div>

                <Space>
                  {!isSessionActive ? (
                    <Button
                      type="primary"
                      icon={<PlayCircleOutlined />}
                      onClick={handleStart}
                      loading={isStarting}
                    >
                      Старт
                    </Button>
                  ) : (
                    <>
                      {isOperatorRole && (
                        (rc.status === "active" || rc.status === "agent_offline" || rc.status === "desktop_locked") ? (
                          <Button
                            danger
                            type="primary"
                            icon={<ControlOutlined />}
                            onClick={async () => {
                              await rc.disable("user_toggle");
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
                              rc.status === "acquiring"
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
                                  "Вы управляете мышью удалённого терминала. Действия ограничены мышью, подтверждаются агентом и журналируются.",
                                okText: "Включить",
                                cancelText: "Отмена",
                                onOk: async () => {
                                  await rc.enable();
                                },
                              });
                            }}
                          >
                            Включить управление
                          </Button>
                        )
                      )}
                      <Button
                        danger
                        icon={<StopOutlined />}
                        onClick={stopSession}
                      >
                        Стоп
                      </Button>
                    </>
                  )}
                </Space>
              </div>

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
                />

                {!isSessionActive && (
                  <div style={{ textAlign: "center", color: "rgba(255,255,255,0.45)" }}>
                    <VideoCameraOutlined style={{ fontSize: 56, marginBottom: 16 }} />
                    <div style={{ fontSize: 16 }}>Трансляция не запущена</div>
                    <div style={{ fontSize: 13, marginTop: 4 }}>
                      Нажмите кнопку «Старт» для подключения к видеопотоку
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
                      isSessionActive
                        ? statusText.includes("Идёт трансляция")
                          ? "processing"
                          : "warning"
                        : "default"
                    }
                  />
                  <Text strong>Статус:</Text>
                  <Text>{statusText}</Text>
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
