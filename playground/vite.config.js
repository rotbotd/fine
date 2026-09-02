import { createReadStream, statSync } from "node:fs";
import path from "node:path";
import { defineConfig } from "vite";

function precompressedZstd() {
  return {
    name: "fine-precompressed-zstd",
    configurePreviewServer(server) {
      server.middlewares.use((request, response, next) => {
        if (request.method !== "GET" && request.method !== "HEAD")
          return next();
        if (!String(request.headers["accept-encoding"] ?? "").split(/\s*,\s*/).includes("zstd"))
          return next();

        let pathname;
        try {
          pathname = decodeURIComponent(new URL(request.url, "http://fine.local").pathname);
        } catch {
          return next();
        }
        if (!pathname.endsWith(".wasm"))
          return next();

        const outputRoot = path.resolve(server.config.root, server.config.build.outDir);
        const relative = pathname.replace(/^\/+/, "");
        const compressed = path.resolve(outputRoot, `${relative}.zst`);
        if (!compressed.startsWith(`${outputRoot}${path.sep}`))
          return next();

        let size;
        try {
          size = statSync(compressed).size;
        } catch {
          return next();
        }

        response.statusCode = 200;
        response.setHeader("Content-Type", "application/wasm");
        response.setHeader("Content-Encoding", "zstd");
        response.setHeader("Content-Length", size);
        response.setHeader("Vary", "Accept-Encoding");
        response.setHeader("Cache-Control", "public, max-age=300, must-revalidate");
        if (request.method === "HEAD")
          return response.end();
        createReadStream(compressed).pipe(response);
      });
    },
  };
}

export default defineConfig({
  plugins: [precompressedZstd()],
  build: {
    outDir: "dist",
    emptyOutDir: true,
    rollupOptions: {
      external(id) {
        return id === "./fine.mjs" || id.endsWith("/fine.mjs");
      },
      output: {
        entryFileNames: "app.js",
        assetFileNames: "[name][extname]",
      },
    },
  },
});
