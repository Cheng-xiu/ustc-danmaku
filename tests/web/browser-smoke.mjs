import { chromium } from '../../web/node_modules/@playwright/test/index.mjs';
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
const out = fileURLToPath(new URL('../../build/web-validation/',import.meta.url));
mkdirSync(out,{recursive:true});
const url=process.env.DEMO_URL??'http://127.0.0.1:4173/';
const qaURL=new URL(url);qaURL.searchParams.set('qa','1');
const report={configVersion:4,abiVersion:3,checks:[],attacks:[],errors:[],passed:false};
let browser;
function check(name,pass,detail){report.checks.push({name,pass:Boolean(pass),detail});if(!pass)throw new Error(name+': '+JSON.stringify(detail));}
try {
 browser=await chromium.launch({channel:'chrome',headless:true});report.browser=await browser.version();
 const context=await browser.newContext({viewport:{width:1440,height:1000}});
 let page=await context.newPage();
 const attachErrors=target=>target.on('pageerror',error=>report.errors.push(error.message));attachErrors(page);
 const inspect=()=>page.evaluate(()=>window.__demo.inspect());
 const leaveArena=()=>page.mouse.move(page.viewportSize().width-1,page.viewportSize().height-1);
 // Keep the resource observation alive with ordinary WASD movement rather
 // than standing in three students' firing lines for the entire charge cycle.
 const survivalRoute=[[760,600],[760,180],[200,180],[200,600]];
 let survivalLeg=0;
 async function runMoving(duration){
  let held=new Set();
  try {
   for(let elapsed=0;elapsed<duration;){
    const current=await inspect();
    if(current.phase!=='playing')throw new Error('Natural charge movement ended early: '+current.phase);
    const boss=current.snapshot.actors[0];let target=survivalRoute[survivalLeg];
    if(Math.hypot(target[0]-boss.x,target[1]-boss.y)<45){survivalLeg=(survivalLeg+1)%survivalRoute.length;target=survivalRoute[survivalLeg];}
    const desired=new Set();const dx=target[0]-boss.x,dy=target[1]-boss.y;
    if(dx>24)desired.add('d');if(dx< -24)desired.add('a');if(dy>24)desired.add('s');if(dy< -24)desired.add('w');
    for(const key of held)if(!desired.has(key))await page.keyboard.up(key);
    for(const key of desired)if(!held.has(key))await page.keyboard.down(key);
    held=desired;const interval=Math.min(200,duration-elapsed);await page.clock.runFor(interval);elapsed+=interval;
   }
  } finally {for(const key of held)await page.keyboard.up(key);}
 }
 const clockTime=new Date('2026-10-07T10:00:00+08:00');
 await page.clock.install({time:clockTime});await page.clock.pauseAt(new Date(clockTime.getTime()+1000));await page.goto(qaURL.href);
 await page.waitForFunction(()=>window.__demo,null,{timeout:30000,polling:50});
 const menu=await inspect();
 check('menu loads current real C/Wasm',menu.snapshot.configVersion===4&&menu.snapshot.costs[3]===100&&menu.snapshot.gpaHalfSaturationKills===20);
 check('menu starts GPA and kills at zero',menu.snapshot.kills===0&&menu.snapshot.gpaHundredths===0);
 await page.locator('[data-overlay="action"]').click();await leaveArena();await page.clock.runFor(100);
 check('start advances fixed ticks',(await inspect()).snapshot.tick>0);
 for(let pattern=0;pattern<4;pattern++){
  await page.keyboard.press('r');await leaveArena();await page.clock.runFor(34);
  if(pattern===3){
   await page.clock.runFor(4000);const charged=await inspect();
   check('fourth attack naturally charges to100',charged.phase==='playing'&&charged.snapshot.energy===100,{phase:charged.phase,energy:charged.snapshot.energy});
  }
  await page.keyboard.down(String(pattern+1));await page.clock.runFor(34);
  let state=await inspect();
  check(`pattern ${pattern} accepted`,state.phase==='playing'&&state.snapshot.pattern===pattern&&state.snapshot.accepted===1,{phase:state.phase,accepted:state.snapshot.accepted,rejected:state.snapshot.rejected,tick:state.snapshot.tick,pattern:state.snapshot.pattern,energy:state.snapshot.energy,reason:state.reason});
  check(`pattern ${pattern} published warning`,state.snapshot.warnings.length>0);
  check(`pattern ${pattern} does not spawn during windup`,state.snapshot.bossBullets===0);
  const acceptedEnergy=state.snapshot.energy;
  await page.clock.runFor(100);
  check(`pattern ${pattern} held key single edge`,(await inspect()).snapshot.accepted===1);
  await page.keyboard.up(String(pattern+1));await page.clock.runFor(1100);
  state=await inspect();check(`pattern ${pattern} emits real bullets`,state.snapshot.bossBullets>0,state.snapshot.bossBullets);
  report.attacks.push({pattern,warningCount:state.snapshot.warnings.length,bossBullets:state.snapshot.bossBullets,energyAfterAccept:acceptedEnergy});
  await page.screenshot({path:out+`pattern-${pattern}.png`});
 }
 await page.keyboard.press('r');await page.clock.runFor(34);
 await page.locator('.demo-skill').nth(0).click();await page.clock.runFor(34);const beforeBusy=await inspect();
 await page.locator('.demo-skill').nth(1).click();await page.clock.runFor(34);const busy=await inspect();
 check('busy request refuses once, no second accepted attack',busy.snapshot.accepted===1&&busy.snapshot.rejected===1&&busy.reason.includes('尚未结束'));
 check('busy refusal does not charge energy',busy.snapshot.energy>=beforeBusy.snapshot.energy);
 await page.keyboard.press('Escape');const paused=(await inspect()).snapshot;await page.clock.runFor(2000);const afterPause=await inspect();
 check('pause freezes tick energy and GPA',afterPause.phase==='paused'&&afterPause.snapshot.tick===paused.tick&&afterPause.snapshot.energy===paused.energy&&afterPause.snapshot.gpaHundredths===paused.gpaHundredths);
 await page.locator('[data-overlay="action"]').click();await leaveArena();await page.clock.runFor(100);
 check('resume does not catch up paused time',(await inspect()).snapshot.tick-paused.tick<=8);
 await page.keyboard.press('r');await leaveArena();await page.clock.runFor(34);
 await page.keyboard.press('4');await page.clock.runFor(34);let poor=await inspect();
 check('fourth attack rejects starting60 without charge',poor.phase==='playing'&&poor.snapshot.accepted===0&&poor.snapshot.rejected===1&&poor.snapshot.energy===60,{energy:poor.snapshot.energy,accepted:poor.snapshot.accepted,rejected:poor.snapshot.rejected});
 await runMoving(4000);await page.keyboard.press('4');await page.clock.runFor(34);const shower=await inspect();
 check('fourth attack accepts100 and spends100',shower.phase==='playing'&&shower.snapshot.accepted===1&&shower.snapshot.pattern===3&&shower.snapshot.energy<2,{energy:shower.snapshot.energy,accepted:shower.snapshot.accepted});
 await runMoving(6500);poor=await inspect();
 check('completed shower never refires automatically',poor.phase==='playing'&&poor.snapshot.accepted===1&&poor.snapshot.attackState===0,{phase:poor.phase,accepted:poor.snapshot.accepted,attackState:poor.snapshot.attackState});
 const beforePoor=poor.snapshot;check('shower has not regained100 after6.5seconds',beforePoor.energy<100,beforePoor.energy);
 await page.keyboard.press('4');await page.clock.runFor(34);poor=await inspect();
 check('repeat shower below100 refuses without charge',poor.phase==='playing'&&poor.snapshot.accepted===beforePoor.accepted&&poor.snapshot.rejected===beforePoor.rejected+1&&poor.snapshot.energy>=beforePoor.energy,{energyBefore:beforePoor.energy,energyAfter:poor.snapshot.energy,accepted:poor.snapshot.accepted,rejected:poor.snapshot.rejected});
 await page.keyboard.press('r');await page.clock.runFor(34);await page.setViewportSize({width:900,height:800});
 const bounds=await page.locator('canvas').boundingBox();await page.mouse.move(bounds.x+720/960*bounds.width,bounds.y+600/720*bounds.height);await page.clock.runFor(250);
 const moved=await inspect();check('scaled pointer moves toward logical target',moved.snapshot.actors[0].x>500,moved.snapshot.actors[0]);
 await leaveArena();await page.clock.runFor(34);const stopped=(await inspect()).snapshot.actors[0];await page.clock.runFor(250);
 check('pointer leaving arena stops movement',Math.abs((await inspect()).snapshot.actors[0].x-stopped.x)<.001);
 await page.keyboard.down('a');await page.clock.runFor(200);await page.keyboard.up('a');check('keyboard fallback moves Boss',(await inspect()).snapshot.actors[0].x<stopped.x-20);
 await page.evaluate(()=>window.dispatchEvent(new Event('blur')));const blurred=await inspect();await page.clock.runFor(500);
 check('focus loss visibly pauses',blurred.phase==='paused'&&(await inspect()).snapshot.tick===blurred.snapshot.tick);
 await page.locator('[data-overlay="action"]').click();await leaveArena();await page.clock.runFor(34);await page.mouse.click(bounds.x+100,bounds.y+100,{button:'right'});
 check('right click pauses',(await inspect()).phase==='paused');
 await page.keyboard.press('r');await leaveArena();await page.clock.runFor(34);const restarted=await inspect();
 check('restart resets HP counters plan wave and GPA',restarted.snapshot.actors[0].hp===6&&restarted.snapshot.accepted===0&&restarted.snapshot.wave===1&&restarted.snapshot.kills===0&&restarted.snapshot.gpaHundredths===0);
 await page.evaluate(()=>window.dispatchEvent(new PageTransitionEvent('pagehide',{persisted:true})));const cached=await inspect();await page.clock.runFor(200);
 check('persisted pagehide handler pauses and keeps core',cached.phase==='paused'&&(await inspect()).snapshot.tick===cached.snapshot.tick);
 await page.evaluate(()=>window.dispatchEvent(new PageTransitionEvent('pageshow',{persisted:true})));await page.locator('[data-overlay="action"]').click();await leaveArena();await page.clock.runFor(100);
 check('persisted pageshow handler resumes RAF after explicit continue',(await inspect()).snapshot.tick>cached.snapshot.tick);
 await page.keyboard.press('r');await leaveArena();await page.clock.runFor(34);await page.clock.runFor(30000);const over=await inspect();
 check('real student fire reaches Boss defeat',over.phase==='over'&&over.snapshot.status===2&&over.snapshot.bossHits===6,{phase:over.phase,status:over.snapshot.status,bossHits:over.snapshot.bossHits,tick:over.snapshot.tick});
 await page.clock.runFor(1000);const frozen=await inspect();check('terminal tick and GPA freeze',frozen.snapshot.tick===over.snapshot.tick&&frozen.snapshot.gpaHundredths===over.snapshot.gpaHundredths);await page.screenshot({path:out+'defeat.png'});await page.close();
 // A new context keeps actual RAF measurements separate from the controlled clock.
 const timingContext=await browser.newContext({viewport:{width:1440,height:1000}});page=await timingContext.newPage();attachErrors(page);
 await page.goto(qaURL.href);await page.waitForFunction(()=>window.__demo,null,{timeout:30000});await page.locator('[data-overlay="action"]').click();await leaveArena();await page.keyboard.press('2');
 await page.waitForTimeout(3000);await page.screenshot({path:out+'playing.png'});report.normalSamples=(await inspect()).samples;await page.keyboard.press('Escape');
 const bench=await timingContext.newPage();attachErrors(bench);await bench.goto(new URL('bench.html',url).href);await bench.waitForFunction(()=>window.__bench,null,{timeout:30000});report.stress=await bench.evaluate(()=>window.__bench);
 check('800 bullet render fixture completed',report.stress.frames===300,report.stress);check('browser has no uncaught runtime errors',report.errors.length===0,report.errors);report.passed=true;
} catch(error){report.failure=error instanceof Error?error.message:String(error);process.exitCode=1;}
finally{writeFileSync(out+'browser-results.json',JSON.stringify(report,null,2));console.log(JSON.stringify({browser:report.browser,checks:report.checks.length,passed:report.checks.filter(c=>c.pass).length,failure:report.failure,attacks:report.attacks,stress:report.stress},null,2));await browser?.close();}
