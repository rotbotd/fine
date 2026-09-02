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
        const plain = path.resolve(outputRoot, relative);
        if (!plain.startsWith(`${outputRoot}${path.sep}`))
          return next();

        const acceptsZstd = String(request.headers["accept-encoding"] ?? "")
          .split(/\s*,\s*/)
          .includes("zstd");
        const selected = acceptsZstd ? `${plain}.zst` : plain;

        let size;
        try {
          size = statSync(selected).size;
        } catch {
          return next();
        }

        response.statusCode = 200;
        response.setHeader("Content-Type", "application/wasm");
        if (acceptsZstd)
          response.setHeader("Content-Encoding", "zstd");
        response.setHeader("Content-Length", size);
        response.setHeader("Vary", "Accept-Encoding");
        response.setHeader("Cache-Control", "public, max-age=300, must-revalidate");
        if (request.method === "HEAD")
          return response.end();
        createReadStream(selected).pipe(response);
      });
    },
  };
}

export default defineConfig({
  plugins: [precompressedZstd()],
  preview: {
    allowedHosts: ["fine.shit.yachts"],
  },
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
