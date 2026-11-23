import react from '@vitejs/plugin-react';
import { defineConfig } from 'vitest/config';

// The data the exporter writes (research/hub_export.py) is served as static files, so the
// page fetches /index.json and /<day>/chart_1s.json. It lives in data/, which git ignores
// (D46): only the code is in the repo.
export default defineConfig({
  plugins: [react()],
  publicDir: '../../data/hub',
  server: { host: '127.0.0.1', port: 5174 }, // local only; botbot keeps 5173
  test: { environment: 'node' },
});
