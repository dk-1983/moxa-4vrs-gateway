/* Production request helpers with deferred fetch; no DOM/backend secrets. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const text=fs.readFileSync('src/web/ui/app.js','utf8');
const section=text.slice(text.indexOf('let webQueue='),text.indexOf('function field('));
const calls=[];const context=vm.createContext({Map,Error,Response,encodeURIComponent,language:'EN',csrf:'',fetch:(path,options)=>new Promise(resolve=>calls.push({path,options,resolve}))});
vm.runInContext(section,context);
function reply(index,csrf='token'){calls[index].resolve(new Response(JSON.stringify({csrf,data:{}}),{status:200}));}
const tick=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 const a=vm.runInContext("api('system')",context),b=vm.runInContext("api('system')",context);assert.strictEqual(a,b);await tick();assert.strictEqual(calls.length,1);
 const post=vm.runInContext("api('logout',{})",context);await tick();assert.strictEqual(calls.length,1);reply(0,'obsolete-session');await a;await tick();assert.strictEqual(calls.length,2);assert.strictEqual(vm.runInContext('csrf',context),'');reply(1,'new-session');await post;
 assert.strictEqual(vm.runInContext('csrf',context),'new-session');
 const next=vm.runInContext("api('system')",context);await tick();assert.strictEqual(calls.length,3);reply(2);await next;
 const busy=vm.runInContext("api('backlight',{enabled:1})",context);await tick();calls[3].resolve(new Response('{}',{status:503,headers:{'Retry-After':'2'}}));
 await assert.rejects(busy,/busy/);assert.strictEqual(calls.length,4);
 const pending=[];for(let i=0;i<8;i++)pending.push(vm.runInContext("webFetch('/test')",context));
 await assert.rejects(vm.runInContext("webFetch('/overflow')",context),/Too many/);
 for(let i=4;i<12;i++){await tick();assert.strictEqual(calls.length,i+1);reply(i);await pending[i-4];}
 console.log('UI requests: one fetch at a time, bounded queue, dedup, CSRF epoch, no POST replay PASS');
})().catch(e=>{console.error(e);process.exitCode=1;});
