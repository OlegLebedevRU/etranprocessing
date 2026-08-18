import { theme } from "antd";
import ruRU from "antd/locale/ru_RU";

const themeConfig = {
  algorithm: theme.defaultAlgorithm,
  token: {
    colorPrimary: "#2563eb",
    colorInfo: "#2563eb",
    colorSuccess: "#16a34a",
    colorWarning: "#d97706",
    colorError: "#dc2626",
    colorTextBase: "#0f172a",
    colorBorderSecondary: "#eceff3",
    colorBgLayout: "#f6f7f9",
    borderRadius: 8,
    borderRadiusLG: 10,
    borderRadiusSM: 6,
    fontSize: 13,
    controlHeight: 30,
    lineHeight: 1.5,
    wireframe: false,
    fontFamily:
      '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif',
  },
  components: {
    Layout: {
      siderBg: "#ffffff",
      headerBg: "#ffffff",
      bodyBg: "#f6f7f9",
      triggerBg: "#ffffff",
      triggerColor: "#0f172a",
    },
    Menu: {
      fontSize: 13,
      itemHeight: 34,
      itemMarginInline: 0,
      itemBorderRadius: 8,
      itemSelectedBg: "#eff4ff",
      itemSelectedColor: "#2563eb",
      itemHoverBg: "#f4f6f8",
      activeBarWidth: 0,
      collapsedIconSize: 15,
      iconSize: 15,
    },
    Table: {
      cellPaddingBlockSM: 7,
      cellPaddingInlineSM: 10,
      headerBg: "#fafbfc",
      headerColor: "#64748b",
      headerSplitColor: "transparent",
      borderColor: "#f1f3f6",
      rowHoverBg: "#f8fafc",
      fontSize: 13,
      headerBorderRadius: 10,
    },
    Card: {
      paddingLG: 16,
      headerFontSize: 14,
      headerHeight: 44,
      headerHeightSM: 40,
      boxShadowTertiary: "0 1px 2px rgba(15, 23, 42, 0.04)",
    },
    Button: {
      fontWeight: 500,
      primaryShadow: "none",
      defaultShadow: "none",
      dangerShadow: "none",
    },
    Segmented: {
      itemSelectedBg: "#ffffff",
      trackBg: "#f1f3f6",
      borderRadius: 8,
    },
    Tabs: {
      horizontalItemGutter: 20,
      titleFontSize: 13,
      cardBg: "#f6f7f9",
    },
    Tag: {
      defaultBg: "#f4f6f8",
      borderRadiusSM: 6,
    },
    Modal: {
      titleFontSize: 15,
      borderRadiusLG: 12,
    },
    Input: {
      paddingBlockSM: 3,
    },
    Statistic: {
      contentFontSize: 22,
      titleFontSize: 12,
    },
  },
};

export { themeConfig, ruRU };
