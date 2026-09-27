import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { guidePage } from './guide-page';

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react(), guidePage()],
  // Only needed if hosted without custom domain
  // base: '/PubRemote/',
  optimizeDeps: {
    exclude: ['lucide-react'],
  },
});
