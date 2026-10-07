import { chromium } from '@playwright/test';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
const packageRoot = path.resolve(process.argv[2]);
const manifest = JSON.parse(readFileSync(path.join(packageRoot, 'manifest.json'),'utf8').replace(/^\uFEFF/,''));
for (const file of manifest.files) {
  const bytes = readFileSync(path.join(packageRoot,file.path));
  const hash = createHash('sha256').update(bytes).digest('hex').toUpperCase();
  if (bytes.length !== file.bytes || hash !== file.sha256) throw Error(`Package mismatch: ${file.path}`);
}
const browser = await chromium.launch({channel:'chrome',headless:true});
try {
  const page = await browser.newPage({viewport:{width:1440,height:1000}});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  const response=await page.goto(process.argv[3]);
  await page.waitForFunction(()=>window.__demo);
  if (!page.url().includes('/dist/?qa=1') || response.status()!==200) throw Error('Subdirectory redirect failed');
  await page.locator('[data-overlay="action"]').click();
  await page.mouse.move(1439,999);
  await page.keyboard.press('4');
  await page.waitForTimeout(300);
  const {phase,snapshot}=await page.evaluate(()=>window.__demo.inspect());
  if (phase!=='playing'||snapshot.tick<=0||snapshot.configVersion!==2||snapshot.accepted!==1||snapshot.pattern!==3||snapshot.warnings.length!==80||errors.length) throw Error('Packaged game failed');
  const report={passed:true,manifestFiles:manifest.files.length,url:page.url(),browser:await browser.version(),phase,tick:snapshot.tick,configVersion:snapshot.configVersion,accepted:snapshot.accepted,warnings:snapshot.warnings.length,errors};
  mkdirSync('build/web-validation',{recursive:true});
  writeFileSync('build/web-validation/package-smoke.json',JSON.stringify(report,null,2));
  console.log(JSON.stringify(report,null,2));
} finally { await browser.close(); }
