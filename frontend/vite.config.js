import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// In dev we proxy REST + WS to the FastAPI backend so the frontend can use
// same-origin relative URLs (/api, /ws). In production the app is served as
// static files and talks to the backend via VITE_API_BASE (see src/api.js).
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:8000',
        changeOrigin: true,
        rewrite: (p) => p.replace(/^\/api/, ''),
      },
      '/ws': {
        target: 'ws://127.0.0.1:8000',
        ws: true,
      },
    },
  },
});