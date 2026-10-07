import { defineConfig } from 'vite';
export default defineConfig({ base: './', build: { target: 'es2022', rollupOptions: { input: { game:'index.html', bench:'bench.html' } } } });
