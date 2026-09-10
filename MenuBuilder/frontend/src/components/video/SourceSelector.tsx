import React from "react";
import { Button, Radio, Select, Space, Spin, Tag, theme, Typography } from "antd";
import {
  DesktopOutlined,
  ReloadOutlined,
  VideoCameraOutlined,
  WarningOutlined,
} from "@ant-design/icons";
import type { CameraSource, DeviceInventory, DisplaySource } from "../../api/video";

const { Text } = Typography;

export interface SourceSelectorProps {
  inventory: DeviceInventory | null;
  selectedSourceKey: string;
  onSelectSourceKey: (key: string) => void;
  selectedProfile: string;
  onChangeProfile: (profile: string) => void;
  loadingInventory: boolean;
  onRefreshInventory: () => void;
  disabled?: boolean;
}

/**
 * Человекочитаемое форматирование имени дисплея (без артефактов вроде desktop #0)
 */
function getDisplayName(disp: DisplaySource, index: number): string {
  const isPrimary = Boolean(disp.is_primary ?? disp.primary);
  let name = disp.name?.trim();

  // Удаляем технические префиксы вида desktop #0 или #0
  if (name) {
    name = name.replace(/desktop\s*#?\d*/gi, "").replace(/#\d+/g, "").trim();
  }

  if (isPrimary) {
    return name ? `Основной экран (${name})` : "Основной экран";
  }

  if (name) {
    return `Экран: ${name}`;
  }

  return `Экран ${index + 1}`;
}

/**
 * Человекочитаемое форматирование имени камеры
 */
function getCameraName(cam: CameraSource, index: number): string {
  const name = cam.name?.trim();
  if (name && !name.startsWith("#") && name !== "0") {
    return name;
  }
  return `Камера ${index + 1}`;
}

export const SourceSelector: React.FC<SourceSelectorProps> = ({
  inventory,
  selectedSourceKey,
  onSelectSourceKey,
  selectedProfile,
  onChangeProfile,
  loadingInventory,
  onRefreshInventory,
  disabled = false,
}) => {
  const { token } = theme.useToken();

  const displays = inventory?.displays || [];
  const cameras = inventory?.cameras || [];
  const hasSources = displays.length > 0 || cameras.length > 0;

  return (
    <div
      style={{
        padding: "12px 16px",
        borderRadius: 8,
        backgroundColor: token.colorFillAlter,
        border: `1px solid ${token.colorBorderSecondary}`,
        display: "flex",
        flexDirection: "column",
        gap: 10,
      }}
    >
      {/* Верхняя строка с заголовком, качеством и кнопкой обновления */}
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          flexWrap: "wrap",
          gap: 8,
        }}
      >
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <Text strong style={{ fontSize: 13 }}>
            Источник видео:
          </Text>
        </div>

        <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
          <Space size="small">
            <Text type="secondary" style={{ fontSize: 12 }}>
              Качество:
            </Text>
            <Select
              size="small"
              value={selectedProfile}
              onChange={onChangeProfile}
              style={{ width: 120 }}
              disabled={disabled}
              options={[
                { label: "720p (HD)", value: "default" },
                { label: "480p (Эконом)", value: "low" },
              ]}
              aria-label="Выбор качества трансляции"
            />
          </Space>

          <Button
            size="small"
            type="link"
            icon={<ReloadOutlined />}
            loading={loadingInventory}
            onClick={onRefreshInventory}
            style={{ padding: 0 }}
          >
            Обновить источники
          </Button>
        </div>
      </div>

      {/* Список источников или спиннер */}
      {loadingInventory ? (
        <div style={{ textAlign: "center", padding: "16px 0" }}>
          <Spin size="small" tip="Опрос видеоустройств терминала..." />
        </div>
      ) : !hasSources ? (
        <div
          style={{
            padding: "12px",
            backgroundColor: token.colorBgContainer,
            borderRadius: 6,
            border: `1px dashed ${token.colorBorder}`,
            display: "flex",
            alignItems: "center",
            gap: 10,
            fontSize: 12,
            color: token.colorTextSecondary,
          }}
        >
          <WarningOutlined style={{ color: token.colorWarning, fontSize: 16 }} />
          <span>
            Источники видеосигнала не обнаружены на терминале. Проверьте подключение монитора или камеры и нажмите «Обновить источники».
          </span>
        </div>
      ) : (
        <Radio.Group
          value={selectedSourceKey}
          onChange={(e) => onSelectSourceKey(e.target.value)}
          disabled={disabled}
          style={{ width: "100%" }}
        >
          <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
            {/* Группа дисплеев */}
            {displays.length > 0 && (
              <div>
                <div
                  style={{
                    fontSize: 11,
                    fontWeight: 600,
                    letterSpacing: 0.5,
                    color: token.colorTextTertiary,
                    marginBottom: 6,
                    textTransform: "uppercase",
                  }}
                >
                  Рабочие столы / Мониторы
                </div>
                <div style={{ display: "flex", flexWrap: "wrap", gap: 10 }}>
                  {displays.map((disp, idx) => {
                    const dispId = disp.id || disp.desktop_id || String(idx);
                    const valueKey = `desktop:${dispId}`;
                    const isPrimary = Boolean(disp.is_primary ?? disp.primary);
                    const res =
                      disp.resolution ||
                      (disp.width && disp.height ? `${disp.width}×${disp.height}` : null);
                    const name = getDisplayName(disp, idx);

                    return (
                      <Radio.Button
                        key={valueKey}
                        value={valueKey}
                        style={{
                          height: "auto",
                          padding: "6px 12px",
                          borderRadius: 6,
                          display: "inline-flex",
                          alignItems: "center",
                        }}
                      >
                        <Space orientation="horizontal" size="small" style={{ fontSize: 12 }}>
                          <DesktopOutlined />
                          <span style={{ fontWeight: 500 }}>{name}</span>
                          {res && (
                            <Tag style={{ margin: 0, fontSize: 11, padding: "0 4px" }}>
                              {res}
                            </Tag>
                          )}
                          {isPrimary && (
                            <Tag color="blue" style={{ margin: 0, fontSize: 11, padding: "0 4px" }}>
                              Основной
                            </Tag>
                          )}
                        </Space>
                      </Radio.Button>
                    );
                  })}
                </div>
              </div>
            )}

            {/* Группа камер */}
            {cameras.length > 0 && (
              <div>
                <div
                  style={{
                    fontSize: 11,
                    fontWeight: 600,
                    letterSpacing: 0.5,
                    color: token.colorTextTertiary,
                    marginBottom: 6,
                    textTransform: "uppercase",
                  }}
                >
                  Камеры
                </div>
                <div style={{ display: "flex", flexWrap: "wrap", gap: 10 }}>
                  {cameras.map((cam, idx) => {
                    const camId = cam.id || cam.camera_id || String(idx);
                    const valueKey = `usb-camera:${camId}`;
                    const name = getCameraName(cam, idx);
                    const isAvailable = cam.available !== false;

                    return (
                      <Radio.Button
                        key={valueKey}
                        value={valueKey}
                        disabled={!isAvailable || disabled}
                        style={{
                          height: "auto",
                          padding: "6px 12px",
                          borderRadius: 6,
                          display: "inline-flex",
                          alignItems: "center",
                        }}
                      >
                        <Space orientation="horizontal" size="small" style={{ fontSize: 12 }}>
                          <VideoCameraOutlined />
                          <span style={{ fontWeight: 500 }}>{name}</span>
                          <Tag
                            color={isAvailable ? "green" : "default"}
                            style={{ margin: 0, fontSize: 11, padding: "0 4px" }}
                          >
                            {isAvailable ? "Готова" : "Недоступна"}
                          </Tag>
                        </Space>
                      </Radio.Button>
                    );
                  })}
                </div>
              </div>
            )}
          </div>
        </Radio.Group>
      )}
    </div>
  );
};

export default SourceSelector;
