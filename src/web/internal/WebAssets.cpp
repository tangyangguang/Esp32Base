#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"
#include "WebGzip.h"

namespace esp32base_web {

const char WEB_CSS_TYPE[] PROGMEM = "text/css; charset=utf-8";
#include "WebCssGzip.inc"

const char WEB_HEAD[] PROGMEM =
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<link rel='stylesheet' href='/esp32base/ui.css?v=" ESP32BASE_WEB_CSS_VERSION "'>"
    "<script>function once(f){if(f.dataset.busy)return false;f.dataset.busy=1;var b=f.querySelector('[type=submit]');if(b)b.disabled=true;return true;}function ebErr(box,msg){if(!box)return;box.textContent=msg||'Action failed';box.classList.add('show');}function ebOpenDialog(id){var d=document.getElementById(id);if(d){d.classList.add('open');var i=d.querySelector('input,select,button');if(i)i.focus();}}function ebCloseDialog(el){var d=el&&el.closest?el.closest('.eb-dialog-backdrop'):null;if(d)d.classList.remove('open');}function ebAjaxSubmit(f){if(!window.fetch)return true;if(f.dataset.busy)return false;f.dataset.busy=1;f.classList.add('eb-busy');var b=f.querySelector('[type=submit]'),e=f.querySelector('[data-eb-error]');if(b)b.disabled=true;if(e){e.textContent='';e.classList.remove('show');}fetch(f.action,{method:f.method||'POST',headers:{'X-Esp32Base-Ajax':'1','Accept':'application/json','Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f)).toString(),credentials:'same-origin'}).then(function(r){return r.json().then(function(j){j.status=r.status;return j;});}).then(function(j){if(!j.ok){ebErr(e,j.error);return;}if(j.target&&j.html){var t=document.getElementById(j.target);if(t)t.outerHTML=j.html;}if(j.close)ebCloseDialog(f);}).catch(function(){ebErr(e,'Request failed.');}).finally(function(){delete f.dataset.busy;f.classList.remove('eb-busy');if(b)b.disabled=false;});return false;}document.addEventListener('submit',function(ev){var f=ev.target;if(f&&f.matches&&f.matches('form[data-eb-ajax]')){ev.preventDefault();ebAjaxSubmit(f);}});document.addEventListener('click',function(ev){var t=ev.target;if(t.matches('dialog[data-eb-light-dismiss]')&&t===ev.target){var r=t.getBoundingClientRect();if(ev.clientX<r.left||ev.clientX>r.right||ev.clientY<r.top||ev.clientY>r.bottom){t.close();return;}}if(t.matches('[data-eb-dialog-open]')){ev.preventDefault();ebOpenDialog(t.getAttribute('data-eb-dialog-open'));}if(t.matches('[data-eb-dialog-close]')){ev.preventDefault();ebCloseDialog(t);}if(t.matches('[data-eb-inline-toggle]')){ev.preventDefault();var e=document.getElementById(t.getAttribute('data-eb-inline-toggle'));if(e)e.classList.add('open');}if(t.matches('[data-eb-inline-close]')){ev.preventDefault();var c=document.getElementById(t.getAttribute('data-eb-inline-close'));if(c)c.classList.remove('open');}});document.addEventListener('keydown',function(ev){if(ev.key=='Escape'){document.querySelectorAll('.eb-dialog-backdrop.open').forEach(function(d){d.classList.remove('open');});}});</script>";

#if ESP32BASE_ENABLE_FS || ESP32BASE_ENABLE_OTA
const char WEB_UPLOAD_HELPERS[] PROGMEM =
    "<script>function ebFmtBytes(n){var u=['B','KB','MB','GB'],i=0,x=n;while(x>=1024&&i<u.length-1){x/=1024;i++;}return (i?x.toFixed(2):Math.round(x))+' '+u[i];}</script>";
#endif

#if ESP32BASE_ENABLE_APP_CONFIG
// Page-scoped CSS for /esp32base/app-config. Kept out of WEB_HEAD so Status, Logs, WiFi,
// Auth, OTA and Tools pages don't pay ~700 B per request for App Config-only styles.
const char WEB_APPCFG_STYLE[] PROGMEM =
    "<style>"
    ".appcfg{margin:0}.appcfg .pagehead,.appcfg .panel{margin:12px 0}.appcfg .panel{padding:12px}.appcfg .acfield{max-width:760px;margin:0 0 11px}.appcfg .acfield:last-child{margin-bottom:0}.appcfg .acfield>label{margin:0 0 3px}.appcfg input:not([type]),.appcfg input[type=text],.appcfg input[type=number],.appcfg select{font-size:14px;padding:6px 8px;margin:0;border:1px solid #ccd5dd;border-radius:7px;box-sizing:border-box;background:#fff;color:#1f2933}.appcfg input:not([type]),.appcfg input[type=text],.appcfg input[type=number]{width:100%}.appcfg select{max-width:260px;width:auto}.appcfg .acrow{display:flex;gap:7px;align-items:center;max-width:760px}.appcfg .acrow input{flex:1 1 auto;min-width:0}.appcfg .acrow input[type=number]{flex:0 1 14ch;width:14ch}.appcfg .unit{color:var(--eb-muted);font-size:13px;flex:0 0 auto}.appcfg .help{color:var(--eb-muted);font-size:13px;margin:4px 0 0}.appcfg .restart{margin-left:4px;font-size:11px;vertical-align:1px}.appcfg .confirmbox{max-height:0;opacity:0;pointer-events:none;transform:translateY(-3px);border:1px solid transparent;background:#fbfcfd;border-radius:8px;padding:0 12px;margin:0;overflow:hidden;transition:opacity .14s ease,transform .18s ease}.appcfg .confirmbox.open{position:fixed;z-index:60;left:50%;top:50%;display:flex;flex-direction:column;width:min(760px,calc(100vw - 28px));max-height:calc(100dvh - 32px);opacity:1;pointer-events:auto;transform:translate(-50%,-50%);border-color:#cbd5df;padding:12px;overflow:hidden;box-shadow:0 18px 46px rgba(16,24,40,.18)}.appcfg .confirmbox h2{margin-bottom:3px}.appcfg .confirmbox .reviewhint{color:var(--eb-muted);font-size:13px;margin:0 0 9px}.appcfg .confirmbox.open .tablewrap{flex:1 1 auto;min-height:0;overflow:auto;border:1px solid var(--eb-line-soft);border-radius:7px;background:#fff}.appcfg .confirmbox table,.appcfg .pendingbox table{border-collapse:collapse;width:100%;font-size:13px}.appcfg .confirmbox th,.appcfg .confirmbox td,.appcfg .pendingbox th,.appcfg .pendingbox td{border-bottom:1px solid var(--eb-line);padding:6px 7px;text-align:left;vertical-align:top}.appcfg .confirmbox th{color:var(--eb-muted);font-weight:650}.appcfg .confirmbox th:nth-child(1){width:34%}.appcfg .confirmbox th:nth-child(2),.appcfg .confirmbox th:nth-child(3){width:33%}.appcfg .confirmbox td:nth-child(3){font-weight:650;color:#25313f}.appcfg .confirmbox tr:last-child td,.appcfg .pendingbox tr:last-child td{border-bottom:0}.appcfg .acgroup th{background:#f3f6f8;color:#526071;font-size:12px;font-weight:700}.appcfg .confirmbox .actions{justify-content:center;padding-top:10px;margin-top:0}.appcfg .confirmbox .cancelbtn{background:#f3f5f5;color:#344054;border:1px solid var(--eb-line)}.appcfg .confirmbox .cancelbtn:hover{background:#e9eef2}.appcfg .acsavebar{min-height:30px;transition:opacity .12s ease}.appcfg .acsavebar.reviewing{visibility:hidden;opacity:0;pointer-events:none}.appcfg .pendingbox{border-color:#efcf96;background:var(--eb-warn-soft);overflow-x:auto}.appcfg #acsavebar,.appcfg .confirmbox .actions{max-width:760px}@media(max-width:640px){.appcfg select{width:100%;max-width:none}.appcfg .acrow{align-items:flex-start}.appcfg .acrow input[type=number]{max-width:100%}.appcfg .unit{padding-top:7px}.appcfg .confirmbox.open{width:calc(100vw - 20px);max-height:calc(100dvh - 20px);padding:11px}.appcfg .confirmbox th,.appcfg .confirmbox td{padding:6px 5px}}"
    "</style>";
#endif

void handleUiCss() {
    markRequest();
    const String encoding = g_server.header("Accept-Encoding");
    sendGzipAsset(g_server, encoding.c_str(), g_server.hasHeader("Accept-Encoding"),
                  WEB_CSS_TYPE, WEB_CSS_GZIP, sizeof(WEB_CSS_GZIP));
}

} // namespace esp32base_web

#endif
