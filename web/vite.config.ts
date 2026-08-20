import { defineConfig } from "vite";
import preact from "@preact/preset-vite";

export default defineConfig({
  plugins: [preact()],
  build: {
    target: "es2020",
    cssCodeSplit: false,
    assetsInlineLimit: 8192,
    rollupOptions: {
      output: {
        entryFileNames: "assets/app.js",
        assetFileNames: (asset) =>
          asset.name?.endsWith(".css") ? "assets/app.css" : "assets/[name][extname]",
      },
    },
  },
});
