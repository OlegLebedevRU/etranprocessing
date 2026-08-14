import { ConfigProvider, theme } from "antd";
import ruRU from "antd/locale/ru_RU";

const { defaultAlgorithm, defaultSeed } = theme;

const themeConfig = {
  algorithm: defaultAlgorithm,
  token: {
    colorPrimary: "#1677ff",
    borderRadius: 8,
    fontFamily:
      '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif',
  },
  components: {
    Layout: {
      siderBg: "#001529",
      headerBg: "#fff",
      bodyBg: "#f5f5f5",
    },
  },
};

export { themeConfig, ruRU };
