import { chromium } from '@playwright/test';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';
const directory=fileURLToPath(new URL('../../build/web-validation/',import.meta.url));mkdirSync(directory,{recursive:true});
const fixturePath=process.argv[3]?path.resolve(process.argv[3]):path.join(directory,'endless-inputs.json');
const nativePath=process.argv[4]?path.resolve(process.argv[4]):path.join(directory,'endless-native-result.json');
const htmlPath=process.argv[2]?path.resolve(process.argv[2]):fileURLToPath(new URL('../../../../outputs/ustc-danmaku-endless-v4.html',import.meta.url));
const started=performance.now();const deadline=started+120000;
const report={method:'Real offline browser WASD/digit input handlers; one controlled logical tick at a time; QA reads only. No injected game state.',configVersion:4,abiVersion:3,success:false,errors:[],externalRequests:[],checks:[],inputsReplayed:0,rafAdvances:0,zeroTickFrames:0};
let browser;
function check(name,condition,detail){report.checks.push({name,pass:Boolean(condition),detail});if(!condition)throw new Error(name+': '+JSON.stringify(detail));}
function requireCondition(condition,message){if(!condition)throw new Error(message);}
function gpa(kills){return Math.floor(430*kills/(kills+20));}
try {
 const fixture=JSON.parse(readFileSync(fixturePath,'utf8'));const native=JSON.parse(readFileSync(nativePath,'utf8'));
 check('fixtures use current cfg4',fixture.config_version===4&&native.config_version===4,{input:fixture.config_version,native:native.config_version});
 const previewMilestone=native.milestones.find(row=>row.wave_index===1&&row.wave_phase===1&&row.waves_cleared===1);
 const birthMilestone=native.milestones.find(row=>row.wave_index===2&&row.wave_phase===0);
 check('native fixture reaches second wave',Boolean(previewMilestone&&birthMilestone));
 check('native warning gap is exactly120ticks',birthMilestone.tick-previewMilestone.tick===120,{clear:previewMilestone.tick,birth:birthMilestone.tick});
 const endTick=birthMilestone.tick+30;
 requireCondition(fixture.inputs.length>=endTick,'Fixture must extend at least 30 ticks beyond second-wave spawn.');
 const milestoneByTick=new Map(native.milestones.filter(row=>row.tick<=endTick).map(row=>[row.tick,row]));
 report.fixture={file:fixturePath,native:nativePath,script:fixture.script,seedLo:fixture.seed_lo,seedHi:fixture.seed_hi,students:fixture.students,previewTick:previewMilestone.tick,birthTick:birthMilestone.tick,endTick};
 browser=await chromium.launch({channel:'chrome',headless:true});report.browser=await browser.version();
 const context=await browser.newContext({viewport:{width:1440,height:1000},offline:true});const page=await context.newPage();
 page.on('pageerror',error=>report.errors.push(error.message));page.on('request',request=>{if(/^https?:/.test(request.url()))report.externalRequests.push(request.url());});
 const inspect=()=>page.evaluate(()=>window.__demo.inspect());
 async function checkVisibleControls(name){
  const layout=await page.evaluate(()=>{
   const hud=document.querySelector('.demo-hud'),viewport=document.querySelector('.viewport');
   const rect=viewport.getBoundingClientRect();
   const scale=rect.width/viewport.offsetWidth;
   const clipTop=rect.top+viewport.clientTop*scale,clipBottom=clipTop+viewport.clientHeight*scale;
   return{height:hud.clientHeight,scrollHeight:hud.scrollHeight,clipTop,clipBottom,buttons:[...hud.querySelectorAll('.demo-system-buttons button')].map(button=>{const b=button.getBoundingClientRect();return{top:b.top,bottom:b.bottom};})};
  });
  check(name,layout.height===720&&layout.scrollHeight===720&&layout.buttons.length===2&&layout.buttons.every(button=>button.top>=layout.clipTop-.05&&button.bottom<=layout.clipBottom+.05),layout);
 }
 // Pause the context while it still has a blank document. Pausing after boot
 // can race an already-running fake RAF callback and move the test clock back.
 const clockTime=new Date('2026-10-07T10:00:00+08:00');
 await page.clock.install({time:clockTime});await page.clock.pauseAt(new Date(clockTime.getTime()+1000));
 const url=pathToFileURL(htmlPath);url.searchParams.set('qa','1');report.url=url.href;
 await page.goto(url.href);await page.waitForFunction(()=>window.__demo,null,{timeout:30000,polling:50});
 await page.locator('[data-overlay="action"]').click({force:true});await page.mouse.move(1439,999);
 let state=await inspect();
 check('start is playing at tick0',state.phase==='playing'&&state.snapshot.tick===0,{phase:state.phase,tick:state.snapshot.tick});
 check('browser core matches fixture seed and counts',state.snapshot.configVersion===4&&state.snapshot.seed===String(fixture.seed_lo)&&fixture.seed_hi===0&&state.snapshot.actors.length-1===fixture.students);
 function compareMilestone(snapshot){
  const expected=milestoneByTick.get(snapshot.tick);if(!expected)return;
  const actual={status:snapshot.status,tick:snapshot.tick,boss_hp:snapshot.actors[0].hp,boss_hits:snapshot.bossHits,student_hits:snapshot.studentHits,kills:snapshot.kills,students_defeated:snapshot.kills,students_deployed:snapshot.studentsDeployed,gpa_hundredths:snapshot.gpaHundredths,wave_index:snapshot.wave,student_count:snapshot.actors.length-1,waves_cleared:snapshot.wavesCleared,wave_phase:snapshot.wavePhase,energy:snapshot.energy,accepted:snapshot.accepted,rejected:snapshot.rejected};
  for(const [key,value] of Object.entries(actual))requireCondition(value===expected[key],`Native milestone tick ${snapshot.tick}: ${key} expected ${expected[key]}, actual ${value}.`);
  report.milestonesMatched=(report.milestonesMatched??0)+1;
 }
 compareMilestone(state.snapshot);
 let held=new Set();let preview=null;let birth=null;
 for(const row of fixture.inputs.slice(0,endTick)){
  requireCondition(performance.now()<deadline,'Replay exceeded the 120-second real time budget.');
  requireCondition(state.phase==='playing'&&state.snapshot.tick===row.tick,`Input ${row.tick} has state ${state.phase}/${state.snapshot.tick}.`);
  requireCondition(!row.pointer_valid,'Keyboard replay requires pointer_valid=0.');
  const desired=new Set();if(row.move_x<-.1)desired.add('a');if(row.move_x>.1)desired.add('d');if(row.move_y<-.1)desired.add('w');if(row.move_y>.1)desired.add('s');
  const divisor=desired.size>1?Math.SQRT2:1;
  const actualX=(Number(desired.has('d'))-Number(desired.has('a')))/divisor;const actualY=(Number(desired.has('s'))-Number(desired.has('w')))/divisor;
  requireCondition(Math.abs(actualX-row.move_x)<1e-6&&Math.abs(actualY-row.move_y)<1e-6,`Input ${row.tick} cannot be reproduced by normalized WASD.`);
  for(const key of held)if(!desired.has(key))await page.keyboard.up(key);for(const key of desired)if(!held.has(key))await page.keyboard.down(key);held=desired;
  for(let pattern=0;pattern<4;pattern++)if(row.attack_mask&(1<<pattern))await page.keyboard.press(String(pattern+1));
  for(let attempts=0;;attempts++){
   requireCondition(attempts<5,`Tick ${row.tick} failed to advance after five RAF intervals.`);
   await page.clock.runFor(16);report.rafAdvances++;state=await inspect();
   if(state.snapshot.tick===row.tick){report.zeroTickFrames++;requireCondition(state.phase==='playing',`Replay paused: ${state.reason}`);continue;}
   requireCondition(state.snapshot.tick===row.tick+1,`Tick ${row.tick} advanced to ${state.snapshot.tick}; multiple logical ticks consumed.`);break;
  }
  report.inputsReplayed++;const s=state.snapshot;
  requireCondition(s.gpaHundredths===gpa(s.kills),`GPA at tick ${s.tick} is not a sole function of kills.`);
  compareMilestone(s);
  if(s.tick===previewMilestone.tick){
   check('first clear remains playing without victory',state.phase==='playing'&&s.status===0&&s.wave===1&&s.wavesCleared===1&&s.kills===fixture.students);
   check('first clear shows four exact spawn markers',s.wavePhase===1&&s.nextWaveStudents===4&&s.spawnPreview.length===4&&s.waveSpawnTick===birthMilestone.tick);
   check('clear GPA matches native count formula',s.gpaHundredths===previewMilestone.gpa_hundredths&&s.gpaHundredths===gpa(s.kills),{kills:s.kills,gpa:s.gpaHundredths});
   check('clear never shows terminal overlay',await page.locator('.demo-status-overlay').isHidden());
   preview={tick:s.tick,spawnTick:s.waveSpawnTick,gpa:s.gpaHundredths,kills:s.kills,points:s.spawnPreview};report.preview=preview;
   await checkVisibleControls('intermission HUD buttons remain inside visible viewport');
   await page.screenshot({path:path.join(directory,'endless-intermission.png')});
  }
  if(preview&&s.tick>preview.tick&&s.tick<preview.spawnTick){
   requireCondition(state.phase==='playing'&&s.status===0&&s.wave===1&&s.wavePhase===1&&s.kills===preview.kills&&s.gpaHundredths===preview.gpa,`Preview state changed early at ${s.tick}.`);
   requireCondition(JSON.stringify(s.spawnPreview)===JSON.stringify(preview.points),`Spawn marker geometry changed at ${s.tick}.`);
   report.previewTicksVerified=(report.previewTicksVerified??0)+1;
  }
  if(s.tick===birthMilestone.tick){
   check('second wave arrives exactly120ticks after clear',preview!==null&&s.tick-preview.tick===120&&state.phase==='playing'&&s.status===0&&s.wave===2&&s.wavePhase===0);
   const actors=s.actors.slice(1);
   check('second wave has four live students',actors.length===4&&actors.every(actor=>actor.alive));
   check('new students appear at the frozen preview points',actors.every((actor,index)=>Math.abs(actor.x-preview.points[index].x)<.002&&Math.abs(actor.y-preview.points[index].y)<.002),actors.map(actor=>({x:actor.x,y:actor.y})));
   check('second wave preserves kills and computed GPA',s.kills===preview.kills&&s.gpaHundredths===preview.gpa&&s.gpaHundredths===birthMilestone.gpa_hundredths);
   birth={tick:s.tick,wave:s.wave,gpa:s.gpaHundredths,kills:s.kills,students:actors.length};report.birth=birth;
   await checkVisibleControls('four-student HUD buttons remain inside visible viewport');
   await page.screenshot({path:path.join(directory,'endless-wave2.png')});
  }
 }
 for(const key of held)await page.keyboard.up(key);
 check('second wave continues for30ticks',birth!==null&&state.phase==='playing'&&state.snapshot.status===0&&state.snapshot.tick===endTick&&state.snapshot.wave===2);
 const hudGpa=await page.locator('[data-ui="gpa"]').textContent();
 check('HUD displays the computed GPA',hudGpa===(state.snapshot.gpaHundredths/100).toFixed(2),hudGpa);
 const layout=await page.locator('.demo-hud').evaluate(node=>({height:node.clientHeight,scrollHeight:node.scrollHeight}));
 check('current HUD fits720px',layout.height===720&&layout.scrollHeight===720,layout);
 await page.keyboard.press('r');await page.clock.runFor(34);const reset=await inspect();
 check('restart clears GPA kills and wave',reset.phase==='playing'&&reset.snapshot.gpaHundredths===0&&reset.snapshot.kills===0&&reset.snapshot.wave===1&&reset.snapshot.wavesCleared===0&&reset.snapshot.actors.length-1===fixture.students);
 check('offline replay makes no external requests',report.externalRequests.length===0,report.externalRequests);
 check('browser reports no runtime errors',report.errors.length===0,report.errors);
 report.final={tick:state.snapshot.tick,wave:state.snapshot.wave,kills:state.snapshot.kills,gpaHundredths:state.snapshot.gpaHundredths,phase:state.phase};report.success=true;
} catch(error){report.failure=error instanceof Error?error.message:String(error);process.exitCode=1;}
finally{report.elapsedSeconds=(performance.now()-started)/1000;writeFileSync(path.join(directory,'endless-browser-result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser?.close();}
