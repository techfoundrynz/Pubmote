import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  // Only needed if hosted without custom domain
  // base: '/PubRemote/',
  optimizeDeps: {
    exclude: ['lucide-react'],
  },
});
