import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import viteCompression from 'vite-plugin-compression'

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
  plugins: [
    preact(),
    viteCompression({
      algorithm: 'gzip',
      ext: '.gz',
      threshold: 1024,
      deleteOriginFile: true,
      compressionOptions: {
        level: 9
      }
    })
  ],
});
