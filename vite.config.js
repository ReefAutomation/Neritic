import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import viteCompression from 'vite-plugin-compression'

export default defineConfig({
  root: 'web',
  build: {
    outDir: '../dist',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        index: './web/index.html'
      },
      output: {
        entryFileNames: '[name].js',
        chunkFileNames: '[name]-[hash].js',
        assetFileNames: ({ names }) => {
          if (names[0]?.endsWith('.css')) return 'style.css';
          return '[name][extname]';
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
