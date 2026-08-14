import { ConfigProvider, theme } from "antd";
import ruRU from "antd/locale/ru_RU";

const themeConfig = {
  algorithm: theme.defaultAlgorithm,
  token: {
    colorPrimary: "#1677ff",
    borderRadius: 6,
    fontSize: 13,
    fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif',
  },
  components: {
    Layout: {
      siderBg: "#001529",
      headerBg: "#fff",
      bodyBg: "#f0f2f5",
    },
    Table: {
      cellPaddingBlockSM: 6,
      cellPaddingInlineSM: 8,
      headerBg: "#fafafa",
      fontSize: 13,
    },
    Card: {
      paddingLG: 12,
    },
    Menu: {
      fontSize: 13,
    },
  },
};

export { themeConfig, ruRU };
