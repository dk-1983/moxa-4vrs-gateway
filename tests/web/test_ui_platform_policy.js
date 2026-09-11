/* Execute production text and System form for each locale, with inert DOM actions. */
const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('fs'),vm=require('vm');
const app=fs.readFileSync('src/web/ui/app.js','utf8');
const extended=fs.readFileSync('src/web/ui/extended.js','utf8');
for(const language of ['EN','RU'])test(`platform recommendation and protocol choice ${language}`,()=>{
 const context=vm.createContext({language,words:{},content:{innerHTML:''},select:(name,label,value,options)=>JSON.stringify({name,value,options}),field:()=>'',t:x=>x,$:()=>({}),submit:()=>{}});
 vm.runInContext(extended.slice(extended.indexOf('const tr='),extended.indexOf('function feedback(')),context);
 const hint=vm.runInContext('protocolHint()',context),description=vm.runInContext('deviceWebDescription()',context);
 assert.match(hint,language==='EN'?/not recommended.*higher CPU usage and connection delays/:/не рекомендуется.*повышенная нагрузка и задержки подключения/);
 assert.match(hint,language==='EN'?/passwords and data.*clear text/:/пароль и данные.*открыто/);
 assert.match(description,/92%/);assert.match(description,language==='EN'?/0\.06%/:/0,06%/);
 assert.match(description,language==='EN'?/separate password-verification process/:/отдельного процесса проверки пароля/);
 vm.runInContext(app.slice(app.indexOf('function system('),app.indexOf("$('language').value=")),context);
 for(const protocol of [0,1]){
  context.content.innerHTML='';context.protocol=protocol;
  vm.runInContext('system({web_enabled:1,web_interface:0,web_protocol:protocol,backlight:1,ntp_enabled:0,ntp_interval:1},"receipt")',context);
  assert.ok(context.content.innerHTML.includes(hint));
  assert.ok(context.content.innerHTML.includes(JSON.stringify({name:'protocol',value:protocol,options:['HTTP','HTTPS (TLS)']})));
 }
 const help=fs.readFileSync(`src/web/ui/help-${language.toLowerCase()}.html`,'utf8');
 assert.match(help,language==='EN'?/not recommended/:/не рекомендуется/);
 assert.match(help,/<strong>92%/);assert.match(help,language==='EN'?/0\.06%/:/0,06%/);
 assert.ok(app.includes('deviceWebDescription().split'));
});
