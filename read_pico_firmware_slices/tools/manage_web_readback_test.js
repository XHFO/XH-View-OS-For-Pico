/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 用真实页面脚本与离线 DOM / API 检查管理流程。/ Exercise the real page script with an offline DOM and API.
 */
'use strict';
const fs=require('node:fs'), vm=require('node:vm'), assert=require('node:assert/strict'), zlib=require('node:zlib');
const html=fs.readFileSync('components/read_pico_transfer/manage.html','utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const KEY='0123456789abcdef0123456789abcdef', SESSION='abcdef0123456789abcdef0123456789';
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const clone=value=>JSON.parse(JSON.stringify(value));
function elements(root){return [root,...root.children.filter(n=>n instanceof Element).flatMap(elements)];}
class Element {
  constructor(tag='div'){this.tagName=tag.toUpperCase();this.children=[];this.attrs={};this.dataset={};this.parent=null;this._value=undefined;this.checked=false;this.disabled=false;this.hidden=false;this.files=[];}
  setAttribute(name,value){value=String(value);this.attrs[name]=value;if(name==='value')this._value=value;else if(name==='checked')this.checked=true;else if(name==='disabled')this.disabled=true;else if(name==='hidden')this.hidden=true;else if(name.startsWith('data-'))this.dataset[name.slice(5).replace(/-([a-z])/g,(_,letter)=>letter.toUpperCase())]=value;else this[name]=value;}
  getAttribute(name){return this.attrs[name]??null;}
  get value(){return this._value??(this.tagName==='SELECT'?this.children.find(n=>n.tagName==='OPTION')?.value:'')??'';}
  set value(value){this._value=String(value);}
  get textContent(){return this.children.map(n=>n instanceof Element?n.textContent:String(n)).join('');}
  set textContent(value){this.replaceChildren(String(value));}
  set innerHTML(_){throw Error('Untrusted content must not use innerHTML');}
  append(...nodes){for(const n of nodes){if(n instanceof Element)n.parent=this;this.children.push(n);}}
  replaceChildren(...nodes){for(const n of this.children)if(n instanceof Element)n.parent=null;this.children=[];if(this.tagName==='SELECT')this._value=undefined;this.append(...nodes);}
  replaceWith(node){const index=this.parent?.children.indexOf(this);assert(index>=0,'replaceWith requires attached node');this.parent.children[index]=node;node.parent=this.parent;this.parent=null;}
  querySelectorAll(selector){const names=selector.split(',').map(s=>s.trim().toUpperCase());return elements(this).slice(1).filter(n=>names.includes(n.tagName));}
  contains(other){return elements(this).includes(other);}
  focus(){if(this.ownerDocument){if(this.ownerDocument.activeElement)this.ownerDocument.activeElement.focused=false;this.ownerDocument.activeElement=this;}this.focused=true;}
  blur(){if(this.ownerDocument?.activeElement===this)this.ownerDocument.activeElement=null;this.focused=false;}
}
function parseDOM(){const root=new Element('document'),stack=[root];const voids=new Set(['META','INPUT','BR','HR','LINK','IMG']);for(const token of html.slice(0,html.indexOf('<script>')).matchAll(/<!--[\s\S]*?-->|<[^>]+>|[^<]+/g)){const text=token[0];if(text.startsWith('<!--')||text.startsWith('<!'))continue;if(text.startsWith('</')){const tag=text.slice(2,-1).trim().toUpperCase();while(stack.length>1){const node=stack.pop();if(node.tagName===tag)break;}}else if(text.startsWith('<')){const match=text.match(/^<([\w-]+)([\s\S]*?)\/?\s*>$/);if(!match)continue;const node=new Element(match[1]);for(const attr of match[2].matchAll(/([\w-]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s'"=<>`]+)))?/g))node.setAttribute(attr[1],attr[2]??attr[3]??attr[4]??'');stack.at(-1).append(node);if(!voids.has(node.tagName))stack.push(node);}else stack.at(-1).append(text);}const document={visibilityState:'visible',activeElement:null,getElementById:id=>elements(root).find(n=>n.id===id)||null,createElement:tag=>{const node=new Element(tag);node.ownerDocument=document;return node;}};for(const node of elements(root))node.ownerDocument=document;return {root,document};}
function homeFixture(){return {config:{enabled:true,local:'http://ha.local:8123',external:'',home_wifi:'Home',external_enabled:false,external_control:false,has_token:true,rooms:[]},status:{result:2,http_status:200,revision:10,busy:false},network_online:true,at_home:true,saved_wifi:['Home','Guest']};}
function printersFixture(){return {printers:[{slot:0,source:2,name:'LAN',serial:'serial-lan',host:'192.168.1.2',port:8883,has_secret:true,has_certificate:true,status:{result:2,has_data:true,percent:30,remaining_minutes:20,state:'打印中'}},{slot:1,source:3,name:'Cloud',serial:'serial-cloud',region:0,has_username:true,has_secret:true,status:{result:2}},{slot:2,source:4,name:'Form',serial:'serial-form',has_client_id:true,has_client_secret:true,status:{result:2}}],imports:[{name:'HA printer',serial:'sensor.printer'}]};}
function bambuFixture(){return {session:0,revision:0,phase:0,region:0,count:0,result:0,error:0,http_status:0,seconds_left:0,retry_after:0,devices:[],config_revision:1,saved_count:1,saved_accounts:[{slot:1,region:0,printer_count:1,name:'Cloud'}],reused:false};}
function entityFixture(){return {items:[{id:'light.desk',name:'台灯',state:'off',room:'书房',kind:0,available:true,on:false,number:null},{id:'climate.room',name:'空调',state:'off',room:'客厅',kind:2,available:true,on:false,number:26},{id:'sensor.temp',name:'温度',state:'23',room:'客厅',kind:4,available:true,on:false,number:23,unit:'°C'}],offset:0,limit:24,total:3,retained:3,total_supported:3,truncated:false,busy:false,revision:10,can_control:true,rooms:['客厅','书房','卧室']};}
async function setup({hash='#key='+KEY,search='',storage={},confirmations=[],home=homeFixture(),printers=printersFixture(),entities=entityFixture(),bambu=bambuFixture(),hook}={}){
 const {root,document}=parseDOM(),saved=new Map(Object.entries(storage));
 const test={requests:[],timers:new Map(),nextTimer:1,replaced:[],confirmations:[],home,printers,entities,bambu,saved};
 const fetch=async(url,options={})=>{assert(url.startsWith('/api/manage/'),'same-origin device API only');test.requests.push({url,options,body:options.body===undefined?undefined:JSON.parse(options.body)});let override=hook?await hook(url,options,test):undefined;if(override==='network')throw Error('secret-not-for-ui');let status=override?.status??200,body=override?.body;
  if(!override){if(url==='/api/manage/home'){if(options.method==='PUT'){const payload=JSON.parse(options.body);Object.assign(home.config,payload);if(payload.ha_token)home.config.has_token=true;delete home.config.ha_token;if(payload.clear?.includes('ha_token'))home.config.has_token=false;body={ok:true,queued:true};}else body=home;}
  else if(url==='/api/manage/printers')body=printers;
  else if(url.startsWith('/api/manage/printers?')){const slot=Number(new URL(url,'http://device').searchParams.get('slot'));if(options.method==='DELETE')printers.printers=printers.printers.map(p=>p.slot===slot?{slot,source:0,name:'',serial:'',status:{result:0}}:p);if(options.method==='PUT'){const payload=JSON.parse(options.body),old=printers.printers.find(p=>p.slot===slot),same=old?.source===payload.source,p={...(same?old:{}),...payload,slot,status:{result:0}};for(const name of ['username','secret','client_id','client_secret','certificate']){if(p[name])p['has_'+name]=true;delete p[name];}printers.printers=printers.printers.filter(p=>p.slot!==slot);printers.printers.push(p);home.config.enabled=true;}body={ok:true,queued:true};}
  else if(url==='/api/manage/bambu/auth'){if(options.method==='POST'){const payload=JSON.parse(options.body);if(payload.session!==bambu.session){status=409;body={error:'stale_session'};}else if(payload.action==='import'){const item=bambu.devices[payload.index];assert(item,'selected bound device exists');printers.printers=printers.printers.filter(p=>p.slot!==payload.slot);printers.printers.push({slot:payload.slot,source:3,name:payload.name||item.name,serial:item.serial,region:bambu.region,has_username:true,has_secret:true,status:{result:0}});home.config.enabled=true;const peers=printers.printers.filter(p=>p.source===3&&p.region===bambu.region);bambu.saved_accounts=[{slot:peers[0].slot,region:bambu.region,printer_count:peers.length,name:peers[0].name}];bambu.saved_count=1;bambu.config_revision++;body={ok:true,queued:true};}else if(payload.action==='cancel'){Object.assign(bambu,{session:bambu.session+1,phase:0,devices:[],count:0});}else if(payload.action==='login'){Object.assign(bambu,{session:bambu.session+1,phase:1,region:payload.region,revision:bambu.revision+1,seconds_left:300});}else if(payload.action==='reuse'){assert.equal(payload.config_revision,bambu.config_revision);const saved=bambu.saved_accounts.find(p=>p.slot===payload.saved_slot);assert(saved);Object.assign(bambu,{session:bambu.session+1,phase:1,region:saved.region,revision:bambu.revision+1,seconds_left:300,reused:true});}else if(payload.action==='email_code'||payload.action==='resend'){Object.assign(bambu,{phase:1,revision:bambu.revision+1});}else throw Error('unhandled Bambu action '+payload.action);}if(!body)body=bambu;}
  else if(url.startsWith('/api/manage/ha/entities?'))body=entities;
  else if(url==='/api/manage/refresh'||url==='/api/manage/ha/control'){body={ok:true,queued:true};status=202;}
  else throw Error('unhandled fixture '+url);}
  return {ok:status>=200&&status<300,status,json:async()=>{if(override?.invalidJSON)throw Error('json secret');return clone(body);}};
 };
 const context=vm.createContext({document,location:{hash,search,pathname:'/manage'},history:{replaceState:(...args)=>test.replaced.push(args[2])},sessionStorage:{getItem:key=>saved.get(key),setItem:(key,value)=>saved.set(key,value),removeItem:key=>saved.delete(key)},fetch,URL,URLSearchParams,TextEncoder,AbortController,setTimeout:(f,ms)=>{const id=test.nextTimer++;test.timers.set(id,{f,ms});return id;},clearTimeout:id=>test.timers.delete(id),confirm:message=>{test.confirmations.push(message);return confirmations.length?confirmations.shift():true;}});
 vm.runInContext('globalThis.window=globalThis;globalThis.events=new Map();globalThis.addEventListener=(name,fn)=>{events.set(name,fn);};',context);
 vm.runInContext(script,context);await flush();await flush();
 const run=(code,values={})=>{Object.assign(context,values);return vm.runInContext(code,context);};
 const node=id=>{const value=document.getElementById(id);assert(value,'node '+id+' exists');return value;};
 const all=(id,tag)=>node(id).querySelectorAll(tag);
 return {test,context,run,node,all,root};
}


(async()=>{
 const failures=[];let total=0;
 const check=async(name,fn)=>{total++;try{await fn();console.log('PASS '+name);}catch(error){failures.push(name+': '+error.message);console.log('FAIL '+name+': '+error.message);}};
 await check('late pre-mutation poll cannot remove a confirmed room on next save',async()=>{
  let held=false,releaseOld,oldSnapshot;
  const t=await setup({hook:(url,opt,test)=>{if(url==='/api/manage/home'&&!opt.method&&held){held=false;oldSnapshot=clone(test.home);return new Promise(resolve=>releaseOld=resolve);}}});
  await t.run('showTab("control")');await flush();held=true;const poll=t.run('poll()');await flush();
  t.run('editRoom(state.entities.items[0])');t.node('roomName').value='First Saved Room';await t.run('saveRoom()');
  releaseOld({status:200,body:oldSnapshot});await poll;
  t.run('editRoom(state.entities.items[1])');t.node('roomName').value='Second Saved Room';await t.run('saveRoom()');
  assert.deepEqual(t.test.home.config.rooms,[{entity:'light.desk',room:'First Saved Room'},{entity:'climate.room',room:'Second Saved Room'}]);
 });
 for(const flagValue of [false,undefined])await check('retain blank HA token must confirm has_token '+String(flagValue),async()=>{
  let submitted=false;
  const t=await setup({hook:(url,opt,test)=>{if(url==='/api/manage/home'&&opt.method==='PUT')submitted=true;if(url==='/api/manage/home'&&!opt.method&&submitted){const data=clone(test.home);if(flagValue===undefined)delete data.config.has_token;else data.config.has_token=flagValue;return {status:200,body:data};}}});
  t.node('haLocal').value='http://changed.local:8123';await t.run('saveHome()');
  assert.equal(t.run('state.dirty.has("home")'),true);assert.match(t.node('homeResult').textContent,/尚未确认/);
 });
 const sources=[{slot:0,flags:['has_secret','has_certificate']},{slot:1,flags:['has_username','has_secret']},{slot:2,flags:['has_client_id','has_client_secret']}];
 for(const {slot,flags} of sources)for(const flag of flags)for(const flagValue of [false,undefined])await check('retain blank printer '+slot+' '+flag+' '+String(flagValue),async()=>{
  let submitted=false;
  const t=await setup({hook:(url,opt,test)=>{if(url.startsWith('/api/manage/printers?')&&opt.method==='PUT')submitted=true;if(url==='/api/manage/printers'&&!opt.method&&submitted){const data=clone(test.printers),p=data.printers.find(p=>p.slot===slot);if(flagValue===undefined)delete p[flag];else p[flag]=flagValue;return {status:200,body:data};}}});
  t.node('p-'+slot+'-name').value='renamed';await t.run('savePrinter('+slot+')');
  assert.equal(t.run('state.dirty.has("p'+slot+'")'),true);assert.match(t.node('p-'+slot+'-result').textContent,/尚未确认/);
 });
 await check('old session poll rejection cannot disconnect a new paired session',async()=>{
  let hold=false,release;
  const t=await setup({hook:(url,opt)=>{if(url==='/api/manage/home'&&!opt.method&&hold){hold=false;return new Promise(resolve=>release=resolve);}}});
  hold=true;const oldPoll=t.run('poll()');await flush();t.run('forgetKey();setKey("11111111111111111111111111111111");state.epoch++');await t.run('loadAll()');assert.equal(t.run('state.connected'),true);
  release({status:200,body:homeFixture()});await oldPoll;assert.equal(t.run('state.connected'),true);assert.equal(t.node('banner').textContent,'');
 });
 await check('clear requires an explicit false credential flag',async()=>{
  let submitted=false;const t=await setup({hook:(url,opt,test)=>{if(url==='/api/manage/home'&&opt.method==='PUT')submitted=true;if(url==='/api/manage/home'&&!opt.method&&submitted){const data=clone(test.home);delete data.config.has_token;return {status:200,body:data};}}});await t.run('clearHome()');assert.equal(t.run('state.dirty.has("home")'),true);assert.match(t.node('homeResult').textContent,/尚未确认/);
 });
 await check('missing printer slot is not confirmation of deletion',async()=>{
  let submitted=false;const t=await setup({hook:(url,opt,test)=>{if(opt.method==='DELETE')submitted=true;if(url==='/api/manage/printers'&&submitted)return {status:200,body:{...clone(test.printers),printers:[]}};}});await t.run('deletePrinter(0)');assert.equal(t.node('p-0-source').value,'2');assert.match(t.node('p-0-result').textContent,/尚未确认/);
 });
 await check('confirmed room save distinguishes a failed entity refresh',async()=>{
  let submitted=false;const t=await setup({hook:(url,opt)=>{if(url==='/api/manage/home'&&opt.method==='PUT')submitted=true;if(url.startsWith('/api/manage/ha/entities?')&&submitted)return 'network';}});await t.run('showTab("control")');await flush();t.run('editRoom(state.entities.items[0])');t.node('roomName').value='Saved';await t.run('saveRoom()');assert.deepEqual(t.test.home.config.rooms,[{entity:'light.desk',room:'Saved'}]);assert.equal(t.run('state.dirty.has("rooms")'),false);assert.equal(t.node('roomForm').hidden,true);assert.match(t.node('entityStatus').textContent,/已保存.*刷新未完成/);
 });
 const departure=t=>clone(t.run('window.readPicoManagementDepartureState()'));
 const unload=t=>clone(t.run('(()=>{const e={prevented:false,returnValue:null,preventDefault(){this.prevented=true;}};events.get("beforeunload")(e);return {prevented:e.prevented,returnValue:e.returnValue};})()'));
 await check('clean browser page and routine polling do not prevent departure',async()=>{
  const t=await setup();await t.run('poll()');assert.deepEqual(departure(t),{dirty:false,working:false});assert.equal(unload(t).prevented,false);
 });
 for(const id of ['haToken','p-0-secret','p-2-client_secret','roomName'])await check('unsaved '+id+' blocks browser departure without exposing or submitting input',async()=>{
  const t=await setup();t.node(id).value='PRIVATE-DRAFT';t.node(id).oninput();const before=t.test.requests.length;
  assert.deepEqual(departure(t),{dirty:true,working:false});assert.deepEqual(unload(t),{prevented:true,returnValue:''});assert.equal(t.test.requests.length,before);assert.equal(t.test.confirmations.length,0);assert.equal(t.node(id).value,'PRIVATE-DRAFT');
 });
 await check('native host owns close protection and browser remains protected after cancelled close',async()=>{
  const t=await setup();t.node('haToken').value='PRIVATE';t.node('haToken').oninput();t.run('Object.defineProperty(window,"__readPicoNativeDepartureGuard",{value:true})');
  assert.equal(unload(t).prevented,false);assert.equal(departure(t).dirty,true);assert.equal(departure(t).dirty,true);
  const browser=await setup();browser.node('haLocal').oninput();assert.equal(unload(browser).prevented,true);assert.equal(unload(browser).prevented,true);
 });
 for(const phase of [1,2,3,4])await check('Bambu phase '+phase+' departure state is accurate',async()=>{
  const t=await setup({bambu:{...bambuFixture(),session:7,phase}});assert.equal(departure(t).working,phase!==4);assert.equal(unload(t).prevented,phase!==4);
 });
 for(const pending of ['state.pending.add("light.desk")','state.working=true','state.bambu.pending=true'])await check('pending operation protects departure '+pending,async()=>{
  const t=await setup();t.run(pending);assert.equal(departure(t).working,true);assert.equal(unload(t).prevented,true);
 });
 for(const action of ['loadAll()','$("disconnect").onclick()'])await check('cancel '+action+' keeps verification session and inputs',async()=>{
  const t=await setup({bambu:{...bambuFixture(),session:7,phase:2},confirmations:[false]});t.node('b-code').value='123456';const before=t.test.requests.length;
  await t.run(action);assert.equal(t.test.confirmations.length,1);assert.equal(t.test.requests.length,before);assert.equal(t.node('b-code').value,'123456');assert.equal(t.run('state.bambu.session'),7);assert.equal(t.run('state.key'),KEY);assert.equal(unload(t).prevented,true);
 });
 await check('confirmed disconnect clears page drafts and unlocks browser departure',async()=>{
  const t=await setup({confirmations:[true]});t.node('haToken').oninput();t.run('$("disconnect").onclick()');assert.equal(t.test.confirmations.length,1);assert.equal(t.run('state.key'),'');assert.deepEqual(departure(t),{dirty:false,working:false});assert.equal(unload(t).prevented,false);
 });
 await check('unsubmitted temperature is protected until cleared or confirmed submission',async()=>{
  const t=await setup();await t.run('showTab("control")');await flush();let input=elements(t.root).find(n=>n.dataset.temperatureEntity==='climate.room');assert(input);input.value='25';input.oninput();assert.equal(departure(t).dirty,true);input.value='';input.oninput();assert.equal(departure(t).dirty,false);input.value='25';input.oninput();await t.run('controlEntity(state.entities.items[1],"set_temperature",25)');assert.equal(departure(t).dirty,false);assert.equal(departure(t).working,true);
 });
 await check('confirmed save clears unload protection while failed save keeps it',async()=>{
  for(const fail of [false,true]){const t=await setup({hook:(url,opt)=>fail&&url==='/api/manage/home'&&opt.method==='PUT'?'network':undefined});t.node('haLocal').value='http://new.local:8123';t.node('haLocal').oninput();await t.run('saveHome()');assert.equal(unload(t).prevented,fail);}
 });
 await check('new edits made during explicit reread are retained',async()=>{
  let hold=false,release;const t=await setup({hook:(url,opt)=>hold&&url==='/api/manage/home'&&!opt.method?(hold=false,new Promise(resolve=>release=resolve)):undefined});hold=true;const reading=t.run('loadAll()');await flush();t.node('haLocal').value='http://new-draft.local:8123';t.node('haLocal').oninput();release({status:200,body:homeFixture()});await reading;assert.equal(t.node('haLocal').value,'http://new-draft.local:8123');assert.equal(departure(t).dirty,true);assert.match(t.node('banner').textContent,/已保留/);
 });
 await check('failed reread preserves accepted-discard drafts for retry',async()=>{
  let fail=false;const t=await setup({hook:(url,opt)=>fail&&url==='/api/manage/home'&&!opt.method?'network':undefined,confirmations:[true]});t.node('haLocal').value='http://draft.local:8123';t.node('haLocal').oninput();fail=true;await t.run('loadAll()');assert.equal(t.node('haLocal').value,'http://draft.local:8123');assert.equal(departure(t).dirty,true);
 });
 await check('temperature drafts survive tab changes and reappear on return',async()=>{
  const t=await setup();await t.run('showTab("control")');await flush();let input=elements(t.root).find(n=>n.dataset.temperatureEntity==='climate.room');input.value='25';input.oninput();t.run('showTab("settings")');assert.equal(departure(t).dirty,true);await t.run('showTab("control")');await flush();input=elements(t.root).find(n=>n.dataset.temperatureEntity==='climate.room');assert.equal(input.value,'25');assert.equal(unload(t).prevented,true);
 });
 await check('old temperature submission does not clear a newly edited value',async()=>{
  let release;const t=await setup({hook:(url,opt)=>url==='/api/manage/ha/control'&&opt.method==='POST'?new Promise(resolve=>release=resolve):undefined});await t.run('showTab("control")');await flush();let input=elements(t.root).find(n=>n.dataset.temperatureEntity==='climate.room');input.value='25';input.oninput();const sending=t.run('controlEntity(state.entities.items[1],"set_temperature",25)');await flush();input=elements(t.root).find(n=>n.dataset.temperatureEntity==='climate.room');input.value='26';input.oninput();release({status:202,body:{ok:true,queued:true}});await sending;assert.equal(t.run('state.temperatureDrafts.get("climate.room")'),'26');assert.equal(departure(t).dirty,true);
 });
 console.log(JSON.stringify({total,failures},null,2));if(failures.length)process.exitCode=1;
})().catch(error=>{console.error(error);process.exitCode=1;});
