import { defineConfig, type Plugin } from "vite";
import react from "@vitejs/plugin-react";
import fs from "node:fs";
import path from "node:path";

function exportMethodCodesPlugin(): Plugin {
  return {
    name: "export-method-codes",
    buildStart() {
      try {
        const srcJsonPath = path.resolve(__dirname, "src/routes/devices/domain/methodCodes.json");
        const publicDir = path.resolve(__dirname, "public");
        const publicJsonPath = path.resolve(publicDir, "methodCodes.json");
        if (fs.existsSync(srcJsonPath)) {
          if (!fs.existsSync(publicDir)) {
            fs.mkdirSync(publicDir, { recursive: true });
          }
          fs.copyFileSync(srcJsonPath, publicJsonPath);
        }
      } catch (err) {
        console.warn("Could not copy methodCodes.json to public:", err);
      }
    },
  };
}

export default defineConfig({
  plugins: [react(), exportMethodCodesPlugin()],
  server: {
    proxy: {
      "/api": "http://localhost:8000",
    },
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks: {
          "vendor-react": ["react", "react-dom", "react-router"],
          "vendor-antd": ["antd", "@ant-design/icons"],
          "vendor-axios": ["axios"],
        },
      },
    },
  },
});
