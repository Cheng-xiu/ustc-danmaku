import { chromium } from '@playwright/test';
import { writeFileSync, mkdirSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
const output = path.resolve('build/web-validation');mkdirSync(output,{recursive:true});
const url = pathToFileURL(path.resolve(process.argv[2])).href+'?qa=1';
const browser=await chromium.launch({channel:'chrome',headless:true});
const report={url,checks:[],errors:[],externalRequests:[]};
function check(name,ok,detail){report.checks.push({name,passed:!!ok,detail});if(!ok)throw Error(name);}
try {
  const context=await browser.newContext({viewport:{width:1440,height:1000},offline:true});
  const page=await context.newPage();
  page.on('pageerror',error=>report.errors.push(error.message));
  page.on('request',request=>{if(/^https?:/.test(request.url()))report.externalRequests.push(request.url());});
  await page.clock.install();await page.goto(url);
  await page.waitForFunction(()=>window.__demo,null,{timeout:30000});
  const inspect=()=>page.evaluate(()=>window.__demo.inspect());
  const initial=await inspect();
  check('single file loads offline real ABI3 core',initial.snapshot.configVersion===4&&initial.snapshot.costs[3]===100&&initial.snapshot.gpaHalfSaturationKills===20);
  check('menu describes endless GPA',await page.locator('[data-overlay="description"]').textContent().then(t=>t.includes('没有胜利')&&t.includes('GPA')));
  await page.screenshot({path:path.join(output,'standalone-menu.png')});
  await page.locator('[data-overlay="action"]').click();await page.mouse.move(1439,999);
  await page.keyboard.press('4');await page.clock.runFor(34);
  const rejected=await inspect();
  check('fourth attack refuses at starting60',rejected.snapshot.accepted===0&&rejected.snapshot.rejected===1&&rejected.snapshot.energy===60,rejected.snapshot.energy);
  await page.clock.runFor(4000);await page.keyboard.press('4');await page.clock.runFor(34);
  const accepted=await inspect();
  check('fourth attack accepts only at100',accepted.snapshot.accepted===1&&accepted.snapshot.pattern===3&&accepted.snapshot.energy<2,accepted.snapshot.energy);
  await page.clock.runFor(1250);const spawned=await inspect();
  check('shower actually emits after windup',spawned.snapshot.bossBullets>=16,spawned.snapshot.bossBullets);
  await page.screenshot({path:path.join(output,'standalone-playing.png')});
  await page.keyboard.press('Escape');const paused=await inspect();await page.clock.runFor(2000);
  const still=await inspect();
  check('pause freezes tick energy GPA',still.phase==='paused'&&still.snapshot.tick===paused.snapshot.tick&&still.snapshot.energy===paused.snapshot.energy&&still.snapshot.gpaHundredths===paused.snapshot.gpaHundredths);
  await page.keyboard.press('r');await page.clock.runFor(34);const reset=await inspect();
  check('restart clears wave and GPA',reset.snapshot.wave===1&&reset.snapshot.kills===0&&reset.snapshot.gpaHundredths===0&&reset.snapshot.actors.length===4);
  await page.clock.runFor(15000);const death=await inspect();
  check('only death gives terminal result',death.phase==='over'&&death.snapshot.status===2&&death.snapshot.actors[0].hp===0);
  check('death report contains finalGPA',await page.locator('[data-overlay="report"]').isVisible());
  await page.clock.runFor(2000);const frozen=await inspect();
  check('death freezes GPA wave time',frozen.snapshot.tick===death.snapshot.tick&&frozen.snapshot.gpaHundredths===death.snapshot.gpaHundredths&&frozen.snapshot.wave===death.snapshot.wave);
  const layout=await page.locator('#hud').evaluate(node=>({height:node.clientHeight,scroll:node.scrollHeight}));
  check('HUD fits720',layout.height===720&&layout.scroll===720,layout);
  await page.screenshot({path:path.join(output,'standalone-death.png')});
  check('offline file has zero external network dependencies',report.externalRequests.length===0,report.externalRequests);
  check('browser has no runtime errors',report.errors.length===0,report.errors);
  report.browser=await browser.version();report.passed=true;
} catch(error){report.failure=String(error);process.exitCode=1;}
finally{writeFileSync(path.join(output,'standalone-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
