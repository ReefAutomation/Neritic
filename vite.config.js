import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';

export default defineConfig({
  root: 'web',
  build: {
    outDir: '../dist',
    emptyOutDir: true,
    rollupOptions: {
      output: {
        entryFileNames: 'index.js',
        chunkFileNames: 'index.js',
        assetFileNames: ({ names }) => {
          if (names[0]?.endsWith('.css')) return 'style.css';
          return '[name]';
        },
      },
    },
  },
  plugins: [preact()],
});
