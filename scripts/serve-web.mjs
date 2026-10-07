import http from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
// Repository builds and the downloadable ZIP intentionally use different layouts.
const defaults = ['../apps/web/dist', '../web/dist'].map(relative => fileURLToPath(new URL(relative, import.meta.url)));
const directory = path.resolve(process.argv[2] ?? defaults.find(candidate => existsSync(path.join(candidate, 'index.html'))) ?? defaults[0]);
const port = Number(process.argv[3] ?? 4173);
const mime = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.svg': 'image/svg+xml', '.json': 'application/json', '.png': 'image/png' };
const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url, 'http://localhost');
    const relative = decodeURIComponent(url.pathname).replace(/^\/+/, '') || 'index.html';
    let filename = path.resolve(directory, relative);
    if (filename !== directory && !filename.startsWith(directory + path.sep)) { res.writeHead(403).end(); return; }
    if ((await stat(filename)).isDirectory()) {
      if (!url.pathname.endsWith('/')) { res.writeHead(308, { Location: url.pathname + '/' + url.search }); res.end(); return; }
      filename = path.join(filename, 'index.html');
    }
    const bytes = await readFile(filename);
    res.writeHead(200, { 'Content-Type': mime[path.extname(filename)] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
    res.end(bytes);
  } catch { res.writeHead(404, { 'Content-Type': 'text/plain' }); res.end('Not found'); }
});
server.on('error', error => { console.error(error.message); process.exitCode = 1; });
server.listen(port, '127.0.0.1', () => {
  const url = `http://127.0.0.1:${port}/`;
  console.log(`Demo: ${url}\nPress Ctrl+C to stop.`);
  if (process.argv.includes('--open') && process.platform === 'win32') {
    const opener = spawn('rundll32.exe', ['url.dll,FileProtocolHandler', url], { windowsHide: true, stdio: 'ignore' });
    opener.on('error', error => console.error(`Open this URL manually: ${url} (${error.message})`));
  }
});
