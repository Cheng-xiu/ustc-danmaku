import { build } from 'esbuild';
import { readFile, readdir, mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
const root = fileURLToPath(new URL('../', import.meta.url));
const web = path.join(root, 'apps/web');
const output = path.resolve(process.argv[2] ?? path.join(root, 'build/release/ustc-danmaku-endless.html'));
const bytesBase64 = (await readFile(path.join(web, 'public/wasm/demo-core.wasm'))).toString('base64');
const bundle = await build({
  stdin: { contents: `import factory from './public/wasm/demo-core.mjs';
    window.__ustcEmbeddedCore={factory,bytesBase64:${JSON.stringify(bytesBase64)}};
    import('./src/main.ts');`, resolveDir: web, sourcefile: 'standalone-entry.js' },
  bundle: true, write: false, format: 'iife', platform: 'browser', target: 'es2022',
  minify: true, sourcemap: false, legalComments: 'inline', outfile: 'standalone.js',
  external: ['node:*'],
  define: { 'import.meta.url': JSON.stringify('file:///ustc-danmaku-endless.html'), 'import.meta.env.BASE_URL': JSON.stringify('./') },
  plugins: [{ name: 'inline-assets', setup(build) {
    build.onResolve({ filter: /\.(jpg|png|svg)\?inline$/ }, args => ({path:path.resolve(args.resolveDir,args.path.replace(/\?inline$/,'')),namespace:'inline-image'}));
    build.onLoad({filter:/.*/,namespace:'inline-image'},async args => {
      const mime=path.extname(args.path)==='.jpg'?'image/jpeg':path.extname(args.path)==='.png'?'image/png':'image/svg+xml';
      const data=(await readFile(args.path)).toString('base64');
      return {contents:`export default ${JSON.stringify(`data:${mime};base64,${data}`)}`,loader:'js'};
    });
  } }],
});
const js = bundle.outputFiles.find(file => file.path.endsWith('.js'))?.text;
const css = bundle.outputFiles.find(file => file.path.endsWith('.css'))?.text ?? '';
if (!js) throw Error('Standalone JavaScript missing');
const lock=JSON.parse(await readFile(path.join(root,'package-lock.json'),'utf8'));
let notices='Production dependency licenses\n';
for (const [name,info] of Object.entries(lock.packages)) {
  if (!name.split('/').includes('node_modules') || info.dev || info.link) continue;
  const directory=path.join(root,name);
  const license=(await readdir(directory)).find(file=>/^(LICENSE|LICENCE|COPYING)(\.|$)/i.test(file));
  const text=license?await readFile(path.join(directory,license),'utf8'):name==='node_modules/@pixi/colord'?await readFile(path.join(root,'references/licenses/colord-LICENSE.md'),'utf8'):null;
  if (!text) throw Error(`Missing license: ${name}`);
  notices+=`\n=== ${name} ${info.version} ===\n${text}\n`;
}
let html=await readFile(path.join(web,'index.html'),'utf8');
html=html.replace(/<script[^>]+src="\/src\/main\.ts"[^>]*><\/script>/,'');
html=html.replace('</head>',`<style>${css.replace(/<\/style/gi,'<\\/style')}</style></head>`);
html=html.replace('</body>',`<script>${js.replace(/<\/script/gi,'<\\/script')}</script><script type="text/plain" id="third-party-notices">${notices.replace(/<\/script/gi,'<\\/script')}</script></body>`);
if (/<script[^>]*\bsrc=|<link[^>]*\bhref=/.test(html)) throw Error('Standalone still references external resources');
await mkdir(path.dirname(output),{recursive:true});
await writeFile(output,html,'utf8');
console.log(JSON.stringify({output,bytes:Buffer.byteLength(html),embeddedWasmBytes:Buffer.from(bytesBase64,'base64').length},null,2));
