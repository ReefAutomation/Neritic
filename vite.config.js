import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';

export default defineConfig({
  root: 'src/preact',
  build: {
    outDir: '../../dist',
    emptyOutDir: true,
    rollupOptions: {
      output: {
        entryFileNames: 'index.js',
        chunkFileNames: 'index.js',
        assetFileNames: ({name}) => {
          if (name && name.endsWith('.css')) return 'style.css';
          return '[name]';
        },
      },
    },
  },
  plugins: [preact()],
});
