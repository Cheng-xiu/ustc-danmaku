import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
const root = fileURLToPath(new URL('../', import.meta.url));
const name = process.argv[2];
const allowed = new Set(['build-native', 'build-wasm', 'setup-web-toolchain', 'package-web', 'package']);
if (!allowed.has(name)) throw Error('Unknown PowerShell task: ' + name);
const executable = process.platform === 'win32' ? 'powershell.exe' : 'pwsh';
const result = spawnSync(executable, ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
  path.join(root, 'scripts', name + '.ps1'), ...process.argv.slice(3)], { cwd: root, stdio: 'inherit', windowsHide: true });
if (result.error) { console.error(`${executable}: ${result.error.message}`); process.exitCode = 1; }
else process.exitCode = result.status ?? 1;
