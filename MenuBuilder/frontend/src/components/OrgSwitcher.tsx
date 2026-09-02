import React, { useEffect, useState } from "react";
import { Button, Grid, Select, Space, Tag, Typography, message } from "antd";
import { SwapOutlined, BankOutlined } from "@ant-design/icons";
import { listAvailableTenants, switchTenant, type OrgItem } from "../api/adminTenants";
import type { UserInfo } from "../api/auth";

const { Text } = Typography;

interface OrgSwitcherProps {
  currentUser: UserInfo | null;
  onTenantSwitched?: () => void;
}

export const OrgSwitcher: React.FC<OrgSwitcherProps> = ({ currentUser, onTenantSwitched }) => {
  const screens = Grid.useBreakpoint();
  const isSuperUser = Boolean(
    currentUser?.is_superuser || currentUser?.can_switch_org || currentUser?.role === "superuser"
  );

  const [orgs, setOrgs] = useState<OrgItem[]>([]);
  const [selectedOrgId, setSelectedOrgId] = useState<number | null>(
    currentUser?.org_id ?? null
  );
  const [loading, setLoading] = useState<boolean>(false);
  const [switching, setSwitching] = useState<boolean>(false);

  useEffect(() => {
    if (!isSuperUser) return;
    let isMounted = true;
    setLoading(true);
    listAvailableTenants()
      .then((data) => {
        if (isMounted) {
          setOrgs(data);
          if (currentUser?.org_id && !selectedOrgId) {
            setSelectedOrgId(currentUser.org_id);
          }
        }
      })
      .catch((err) => {
        console.error("Failed to load organizations:", err);
      })
      .finally(() => {
        if (isMounted) setLoading(false);
      });
    return () => {
      isMounted = false;
    };
  }, [isSuperUser, currentUser?.org_id]);

  // STRICT SECURITY & UI RULE: Never render for regular users
  if (!isSuperUser) {
    return null;
  }

  const handleSwitch = async () => {
    if (!selectedOrgId) return;
    if (selectedOrgId === currentUser?.org_id) {
      message.info("Вы уже находитесь в контексте этой организации");
      return;
    }

    setSwitching(true);
    try {
      const result = await switchTenant(selectedOrgId);
      localStorage.setItem("mb_token", result.access_token);
      localStorage.setItem("mb_current_org_id", String(result.org_id));
      localStorage.setItem("mb_current_org_name", result.org_name);
      if (result.timezone) {
        localStorage.setItem("org_timezone", result.timezone);
      }
      message.success(`Контекст переключен на: ${result.org_name} (ID: ${result.org_id})`);

      if (onTenantSwitched) {
        onTenantSwitched();
      } else {
        window.location.reload();
      }
    } catch (err: unknown) {
      const errorMsg = err instanceof Error ? err.message : "Ошибка смены организации";
      message.error(errorMsg);
    } finally {
      setSwitching(false);
    }
  };

  const currentOrgDisplayName =
    currentUser?.org_name ||
    orgs.find((o) => o.org_id === currentUser?.org_id)?.org_name ||
    (currentUser?.org_id ? `Организация #${currentUser.org_id}` : "Не выбрана");

  const isMobile = !screens.md;
  const isXs = screens.xs;

  return (
    <div
      style={{
        display: "flex",
        alignItems: "center",
        gap: isXs ? 4 : 8,
        background: "#f0f5ff",
        border: "1px solid #adc6ff",
        borderRadius: 6,
        padding: isXs ? "2px 4px" : "3px 8px",
        maxWidth: "100%",
      }}
    >
      <Tag color="blue" icon={<BankOutlined />} style={{ margin: 0, maxWidth: isXs ? 110 : 200 }} title={currentOrgDisplayName}>
        {isXs ? currentOrgDisplayName : `Тенант: ${currentOrgDisplayName}`}
      </Tag>
      <Select
        size="small"
        showSearch
        placeholder="Сменить..."
        style={{ width: isXs ? 110 : isMobile ? 150 : 200 }}
        loading={loading}
        value={selectedOrgId}
        onChange={setSelectedOrgId}
        filterOption={(input, option) =>
          (option?.label ?? "").toLowerCase().includes(input.toLowerCase())
        }
        options={orgs.map((o) => ({
          value: o.org_id,
          label: `${o.org_name} (ID: ${o.org_id})`,
        }))}
      />
      <Button
        type="primary"
        size="small"
        icon={<SwapOutlined />}
        loading={switching}
        onClick={handleSwitch}
        disabled={!selectedOrgId || selectedOrgId === currentUser?.org_id}
      >
        {!isXs && "Войти"}
      </Button>
    </div>
  );
};
