#include "web_portal.h"

#include <Update.h>
#include <WiFi.h>
#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>

#include "EPD_13in3e.h"
#include "audio.h"
#include "battery.h"
#include "device_config.h"
#include "device_info.h"
#include "remote_log.h"
#include "wifi_provisioning.h"

// Visual design (CSS/layout/copy) ported from the user's Fraimic_Clone
// project (github.com/dsackr/OSHA-7.3eink), a web portal for a different
// board that already reverse-engineered the real Fraimic app's look and
// feel. Trimmed here to what applies to this board: no orientation toggle
// (the EPD_13IN3E panel is a fixed 1200x1600 layout, see EPD_13in3e.h), no
// fuel-gauge recalibration controls (this board reads battery over a
// resistor-divider ADC, not a MAX17048), no SD image gallery (SD is
// mount-only in this firmware so far).
namespace web_portal {

namespace {

String escJ(const String &s) {
    String out;
    out.reserve(s.length() + 4);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

const char CSS[] =
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:#ECEADE;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "min-height:100vh;display:flex;justify-content:center;align-items:flex-start;padding:24px 16px}"
    ".card{background:#fff;border-radius:20px;padding:28px 24px;max-width:420px;width:100%;"
    "box-shadow:0 2px 16px rgba(0,0,0,.06)}"
    "h1{text-align:center;font-size:22px;font-weight:700;color:#2C2825;margin-bottom:6px}"
    ".sub{text-align:center;font-size:13px;color:#8C8882;margin-bottom:20px}"
    "a{color:#B8964A;text-decoration:none}"
    "label{display:block;font-size:13px;font-weight:600;color:#4C4742;margin-bottom:6px}"
    "select,input[type=text],input[type=password]{width:100%;padding:11px 14px;border:1.5px solid #DDD8D0;"
    "border-radius:10px;font-size:15px;color:#2C2825;background:#fff;outline:none;"
    "-webkit-appearance:none;appearance:none}"
    "select:focus,input:focus{border-color:#B8964A}"
    ".group{margin-bottom:16px}"
    ".btn{display:block;width:100%;padding:14px;background:#B8964A;color:#fff;border:none;"
    "border-radius:12px;font-size:16px;font-weight:600;cursor:pointer;margin-top:4px}"
    ".btn:hover{background:#A0843A}.btn:disabled{opacity:.6;cursor:default}"
    ".hint{text-align:center;font-size:12px;color:#9C9490;margin-top:14px}"
    ".back{display:block;text-align:center;margin-top:14px;font-size:13px}"
    ".foot{text-align:center;font-size:12px;color:#A8A09A;margin-top:4px}"
    ".ic{width:56px;height:56px;border-radius:50%;background:#EAE6E0;display:flex;"
    "align-items:center;justify-content:center;margin:0 auto 14px}"
    ".ic svg{width:26px;height:26px}";

const char SVG_WIFI[] =
    "<svg viewBox='0 0 24 24' fill='none' stroke='#B8964A' stroke-width='2' stroke-linecap='round'>"
    "<path d='M1.42 9a16 16 0 0 1 21.16 0'/>"
    "<path d='M5 12.55a11 11 0 0 1 14.08 0'/>"
    "<path d='M8.53 16.11a6 6 0 0 1 6.95 0'/>"
    "<circle cx='12' cy='20' r='1.2' fill='#B8964A' stroke='none'/>"
    "</svg>";

const char SVG_UPLOAD[] =
    "<svg viewBox='0 0 24 24' fill='none' stroke='#B8964A' stroke-width='2'"
    " stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4'/>"
    "<polyline points='17 8 12 3 7 8'/>"
    "<line x1='12' y1='3' x2='12' y2='15'/>"
    "</svg>";

const char SVG_PLUS[] =
    "<svg viewBox='0 0 24 24' fill='none' stroke='#B8964A' stroke-width='2' stroke-linecap='round'>"
    "<circle cx='12' cy='12' r='10'/>"
    "<line x1='12' y1='8' x2='12' y2='16'/>"
    "<line x1='8' y1='12' x2='16' y2='12'/>"
    "</svg>";

const char SVG_HOME[] =
    "<svg viewBox='0 0 24 24' fill='none' stroke='#B8964A' stroke-width='2'"
    " stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/>"
    "<polyline points='9,22 9,12 15,12 15,22'/>"
    "</svg>";

const char SVG_TERMINAL[] =
    "<svg viewBox='0 0 24 24' fill='none' stroke='#B8964A' stroke-width='2'"
    " stroke-linecap='round' stroke-linejoin='round'>"
    "<rect x='2' y='4' width='20' height='16' rx='2'/>"
    "<path d='M6 9l4 3-4 3'/><path d='M13 15h5'/>"
    "</svg>";

const char SVG_CHIP[] =
    "<svg viewBox='0 0 24 24' fill='none' stroke='#B8964A' stroke-width='2'"
    " stroke-linecap='round' stroke-linejoin='round'>"
    "<rect x='6' y='6' width='12' height='12' rx='2'/>"
    "<path d='M9 2v3'/><path d='M15 2v3'/><path d='M9 19v3'/><path d='M15 19v3'/>"
    "<path d='M2 9h3'/><path d='M2 15h3'/><path d='M19 9h3'/><path d='M19 15h3'/>"
    "</svg>";

// ============================================================
// GET / and GET /portal — main portal page
// ============================================================

void handlePortal(AsyncWebServerRequest *request) {
    bool conn = WiFi.status() == WL_CONNECTED;
    battery::Status bat = battery::read();

    String html;
    html.reserve(4096);
    html = "<!DOCTYPE html><html lang='en'><head>"
           "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Fraimic Portal</title><style>";
    html += CSS;
    html += ".status{display:flex;gap:8px;align-items:center;justify-content:center;flex-wrap:wrap;"
            "background:#F6F4F0;border-radius:12px;padding:10px 16px;margin-bottom:20px;"
            "font-size:13px;color:#5C5752}"
            ".status span{white-space:nowrap}"
            ".ok{color:#4CAF50;font-weight:600}.bad{color:#E57373;font-weight:600}"
            ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:20px}"
            ".tile{display:block;background:#fff;border:1.5px solid #E8E4DE;border-radius:16px;"
            "padding:20px 14px;text-align:center;text-decoration:none;color:inherit;"
            "transition:background .15s}"
            ".tile:hover{background:#F6F4F0}"
            ".tile h2{font-size:14px;font-weight:600;color:#2C2825;margin-bottom:5px}"
            ".tile p{font-size:12px;color:#8C8882;line-height:1.4}"
            "</style></head><body><div class='card'>"
            "<h1>Fraimic Portal</h1>"
            "<div class='status'>";

    html += "<span>WiFi: <span class='";
    html += conn ? "ok'>Connected" : "bad'>Disconnected";
    html += "</span></span><span>Mode: Normal</span>";
    if (bat.charging) html += "<span>&#x26A1; ";
    else html += "<span>";
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2fV (%d%%)", bat.voltage_mv / 1000.0f, bat.percent);
    html += buf;
    html += "</span>";
    html += "</div><div class='grid'>";

    html += "<a class='tile' href='/wifi'><div class='ic'>";
    html += SVG_WIFI;
    html += "</div><h2>WiFi</h2><p>Configure network connection settings</p></a>";

    html += "<a class='tile' href='/upload'><div class='ic'>";
    html += SVG_UPLOAD;
    html += "</div><h2>Upload</h2><p>Upload a .bin file to display custom artwork</p></a>";

    html += "<a class='tile' href='https://ha.dalesackrider.com/config/integrations/integration/fraimic' target='_blank'>"
            "<div class='ic'>";
    html += SVG_PLUS;
    html += "</div><h2>Device Setup</h2><p>Add this frame to Home Assistant</p></a>";

    html += "<a class='tile' href='https://ha.dalesackrider.com/fraimic' target='_blank'>"
            "<div class='ic'>";
    html += SVG_HOME;
    html += "</div><h2>Home Assistant</h2><p>Open your Home Assistant dashboard</p></a>";

    html += "<a class='tile' href='/logs'><div class='ic'>";
    html += SVG_TERMINAL;
    html += "</div><h2>Logs</h2><p>View live device logs over WiFi</p></a>";

    html += "<a class='tile' href='/ota'><div class='ic'>";
    html += SVG_CHIP;
    html += "</div><h2>Firmware Update</h2><p>Upload a new firmware .bin</p></a>";

    html += "</div>"
            "<div class='foot'>ESP32-S3-ePaper-13.3E6 &bull; <a href='/info'>Device Information</a></div>"
            "</div></body></html>";

    request->send(200, "text/html", html);
}

// ============================================================
// GET /wifi — WiFi settings page (works while connected in STA mode,
// unlike the AP-only captive portal in wifi_provisioning.cpp)
// ============================================================

void handleWifiPage(AsyncWebServerRequest *request) {
    String html;
    html.reserve(4096);
    html = "<!DOCTYPE html><html lang='en'><head>"
           "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Set Up WiFi</title><style>";
    html += CSS;
    html += ".scan-msg{text-align:center;font-size:13px;color:#B8964A;font-style:italic;"
            "margin-bottom:18px;min-height:18px}"
            ".manual-link{display:block;text-align:center;font-size:13px;color:#B8964A;"
            "margin-top:8px;cursor:pointer;background:none;border:none;width:100%}"
            ".pw-wrap{position:relative}"
            ".pw-wrap input{padding-right:42px}"
            ".eye-btn{position:absolute;right:12px;top:50%;transform:translateY(-50%);"
            "background:none;border:none;cursor:pointer;padding:4px;color:#8C8882}"
            ".msg{padding:10px 14px;border-radius:10px;font-size:13px;margin-bottom:14px;display:none}"
            ".msg.ok{background:#E8F5E9;color:#2E7D32;display:block}"
            ".msg.err{background:#FFEBEE;color:#B71C1C;display:block}"
            "</style></head><body><div class='card'>"
            "<div class='ic'>";
    html += SVG_WIFI;
    html += "</div><h1>Set Up WiFi</h1>"
            "<div class='scan-msg' id='scanMsg'>Scanning for networks...</div>"
            "<div id='msgBox' class='msg'></div>"
            "<form id='wf'>"
            "<div class='group'>"
            "<label>Network Name (SSID)</label>"
            "<select id='netSel' name='ssid'><option value=''>Select your WiFi network</option></select>"
            "<button type='button' class='manual-link' id='manBtn' onclick='toggleManual()'>"
            "Enter network name manually</button>"
            "<input type='text' id='manInput' name='ssid' placeholder='Enter network name manually'"
            " style='display:none;margin-top:8px' disabled>"
            "</div>"
            "<div class='group'>"
            "<label>WiFi Password</label>"
            "<div class='pw-wrap'>"
            "<input type='password' id='pw' name='password' placeholder='Enter WiFi password'>"
            "<button type='button' class='eye-btn' onclick='togglePw()'>"
            "<svg viewBox='0 0 24 24' width='18' height='18' fill='none' stroke='currentColor' stroke-width='2'>"
            "<path d='M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z'/>"
            "<circle cx='12' cy='12' r='3'/></svg>"
            "</button></div></div>"
            "<button type='submit' class='btn' id='saveBtn'>Save Credentials</button>"
            "</form>"
            "<div class='hint'>Make sure your device is within range of your WiFi router</div>"
            "<a class='back' href='/portal'>Portal</a>"
            "</div>"
            "<script>"
            "var manual=false;"
            "function toggleManual(){"
            "manual=!manual;"
            "document.getElementById('netSel').style.display=manual?'none':'block';"
            "document.getElementById('netSel').disabled=manual;"
            "document.getElementById('manInput').style.display=manual?'block':'none';"
            "document.getElementById('manInput').disabled=!manual;"
            "document.getElementById('manBtn').textContent=manual?'Choose from scanned networks':'Enter network name manually';"
            "}"
            "function togglePw(){"
            "var p=document.getElementById('pw');"
            "p.type=p.type==='password'?'text':'password';"
            "}"
            "function loadNets(){"
            "fetch('/wifi/scan.json').then(r=>r.json()).then(nets=>{"
            "var sel=document.getElementById('netSel'),seen={};"
            "nets.forEach(n=>{"
            "if(!seen[n.ssid]){"
            "seen[n.ssid]=1;"
            "var o=document.createElement('option');"
            "o.value=n.ssid;"
            "var bars=n.rssi>-60?'\\u2582\\u2584\\u2586\\u2588':n.rssi>-75?'\\u2582\\u2584\\u2586':n.rssi>-85?'\\u2582\\u2584':'\\u2582';"
            "o.textContent=n.ssid+'  '+bars;"
            "sel.appendChild(o);"
            "}"
            "});"
            "document.getElementById('scanMsg').textContent=nets.length+' network'+(nets.length!==1?'s':'')+' found';"
            "}).catch(()=>document.getElementById('scanMsg').textContent='Scan failed — enter manually');"
            "}"
            "document.getElementById('wf').addEventListener('submit',function(e){"
            "e.preventDefault();"
            "var ssid=manual?document.getElementById('manInput').value:document.getElementById('netSel').value;"
            "var pass=document.getElementById('pw').value;"
            "if(!ssid){showMsg('Please select or enter a network name',false);return;}"
            "document.getElementById('saveBtn').textContent='Saving...';"
            "document.getElementById('saveBtn').disabled=true;"
            "fetch('/wifi/save',{method:'POST',"
            "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
            "body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pass)"
            "}).then(()=>showMsg('Saved! Device is restarting…',true))"
            ".catch(()=>showMsg('Saved! Device is restarting…',true));"
            "});"
            "function showMsg(m,ok){"
            "var el=document.getElementById('msgBox');"
            "el.className='msg '+(ok?'ok':'err');el.textContent=m;"
            "}"
            "loadNets();"
            "</script></body></html>";

    request->send(200, "text/html", html);
}

void handleWifiScanJson(AsyncWebServerRequest *request) {
    WiFi.setSleep(false);
    int n = WiFi.scanNetworks(false, false);  // blocking ~2-4s
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + escJ(WiFi.SSID(i)) + "\"";
        json += ",\"rssi\":" + String(WiFi.RSSI(i));
        json += "}";
    }
    json += "]";
    WiFi.scanDelete();
    request->send(200, "application/json", json);
}

// Fires the actual credential save + restart shortly after the response is
// queued, mirroring fraimic_api.cpp's deferredAction — restarting inside the
// request handler itself risks cutting the async response off mid-flush.
void deferredWifiSave(const String &ssid, const String &password) {
    struct Ctx {
        String ssid;
        String password;
    };
    auto *ctx = new Ctx{ssid, password};
    xTaskCreate(
        [](void *arg) {
            auto *c = static_cast<Ctx *>(arg);
            vTaskDelay(pdMS_TO_TICKS(500));
            wifi_provisioning::saveCredentialsAndRestart(c->ssid, c->password);
            delete c;
            vTaskDelete(nullptr);
        },
        "wifi_save", 4096, ctx, 1, nullptr);
}

void handleWifiSave(AsyncWebServerRequest *request) {
    if (!request->hasParam("ssid", true) || request->getParam("ssid", true)->value().isEmpty()) {
        request->send(400, "text/plain", "Missing SSID");
        return;
    }
    String ssid = request->getParam("ssid", true)->value();
    String password = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
    request->send(200, "text/plain", "Saved. Restarting...");
    deferredWifiSave(ssid, password);
}

// ============================================================
// GET /upload + POST /upload — manual .bin upload, displayed immediately.
// Uses its own buffer (not fraimic_api's /api/image buffer) so the two
// paths can't interfere with each other.
// ============================================================

uint8_t *uploadImageBuf = nullptr;
size_t uploadBytesReceived = 0;
bool uploadValid = false;

void handleUploadPage(AsyncWebServerRequest *request) {
    String html;
    html.reserve(3000);
    html = "<!DOCTYPE html><html lang='en'><head>"
           "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Upload Image</title><style>";
    html += CSS;
    html += ".drop{border:2px dashed #DDD8D0;border-radius:12px;padding:22px 16px;text-align:center;"
            "cursor:pointer;transition:border .2s,background .2s}"
            ".drop:hover,.drop.over{border-color:#B8964A;background:#FBF9F5}"
            ".drop-label{font-size:14px;color:#5C5752;pointer-events:none}"
            ".drop-sub{font-size:12px;color:#9C9490;margin-top:4px;pointer-events:none}"
            "#prog{text-align:center;font-size:13px;margin-top:12px;min-height:18px}"
            "</style></head><body><div class='card'>"
            "<div class='ic'>";
    html += SVG_UPLOAD;
    html += "</div><h1>Upload Image</h1>"
            "<p class='sub'>Upload a .bin image file to display on your e-paper frame</p>"
            "<form id='uf'>"
            "<div class='group'>"
            "<label>Select Image File</label>"
            "<div class='drop' id='drop' onclick='document.getElementById(\"fi\").click()'>"
            "<div class='drop-label' id='dropLabel'>Click to choose a .bin file</div>"
            "<div class='drop-sub' id='dropSub'>Max file size: 960KB (.bin format only)</div>"
            "</div>"
            "<input type='file' id='fi' name='image' accept='.bin' style='display:none'>"
            "</div>"
            "<button type='submit' class='btn' id='upBtn'>Upload Image</button>"
            "</form>"
            "<div id='prog'></div>"
            "<div class='hint'>Only .bin image files (960KB for 1200&#xD7;1600 Spectra-6) are supported</div>"
            "<a class='back' href='/portal'>Portal</a>"
            "</div>"
            "<script>"
            "document.getElementById('fi').addEventListener('change',function(){"
            "var f=this.files[0];"
            "if(f){"
            "document.getElementById('dropLabel').textContent=f.name;"
            "document.getElementById('dropSub').textContent=(f.size/1024).toFixed(1)+'KB';"
            "}"
            "});"
            "document.getElementById('uf').addEventListener('submit',function(e){"
            "e.preventDefault();"
            "var file=document.getElementById('fi').files[0];"
            "if(!file){alert('Please select a .bin file');return;}"
            "var btn=document.getElementById('upBtn'),prog=document.getElementById('prog');"
            "btn.disabled=true;btn.textContent='Uploading...';"
            "prog.style.color='#B8964A';prog.textContent='Sending to display…';"
            "var fd=new FormData();fd.append('image',file);"
            "var xhr=new XMLHttpRequest();"
            "xhr.open('POST','/upload',true);"
            "xhr.onload=function(){"
            "if(xhr.status===200){"
            "prog.style.color='#4CAF50';prog.textContent='Image displayed successfully!';"
            "}else{"
            "prog.style.color='#E53935';prog.textContent='Upload failed: '+xhr.status;"
            "}"
            "btn.disabled=false;btn.textContent='Upload Image';"
            "};"
            "xhr.onerror=function(){"
            "prog.style.color='#E53935';prog.textContent='Connection lost (device may be refreshing display)';"
            "btn.disabled=false;btn.textContent='Upload Image';"
            "};"
            "xhr.send(fd);"
            "});"
            "</script></body></html>";

    request->send(200, "text/html", html);
}

void handleUploadFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len,
                       bool final) {
    (void)request;
    (void)filename;
    (void)final;
    if (index == 0) {
        uploadBytesReceived = 0;
        uploadValid = uploadImageBuf != nullptr;
    }
    if (uploadValid && uploadBytesReceived + len <= EPD_13IN3E_FRAIMIC_BIN_BYTES) {
        memcpy(uploadImageBuf + uploadBytesReceived, data, len);
    }
    uploadBytesReceived += len;
}

void handleUploadDone(AsyncWebServerRequest *request) {
    bool ok = uploadValid && uploadBytesReceived == EPD_13IN3E_FRAIMIC_BIN_BYTES;
    if (ok) {
        EPD_13IN3E_DisplayFraimicBin(uploadImageBuf, EPD_13IN3E_FRAIMIC_BIN_BYTES);
    }
    request->send(ok ? 200 : 400, "application/json",
                  ok ? "{\"status\":\"ok\"}" : "{\"error\":\"invalid image size\"}");
    uploadValid = false;
    uploadBytesReceived = 0;
}

// ============================================================
// GET /info — device information page
// ============================================================

void handleInfoPage(AsyncWebServerRequest *request) {
    String mac = WiFi.macAddress();
    String deviceKey = device_config::deviceKey();
    battery::Status bat = battery::read();
    bool conn = WiFi.status() == WL_CONNECTED;

    String html;
    html.reserve(4096);
    html = "<!DOCTYPE html><html lang='en'><head>"
           "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Information - Fraimic</title><style>";
    html += CSS;
    html += ".pg{padding:0}.pg h1{margin-bottom:20px}"
            ".sec{border:1.5px solid #E8E4DE;border-radius:14px;margin-bottom:16px;overflow:hidden}"
            ".sec-hd{font-size:11px;font-weight:700;letter-spacing:.08em;color:#B8964A;"
            "padding:10px 16px;background:#FAF8F4;border-bottom:1px solid #EEEAE4}"
            ".row{display:flex;justify-content:space-between;align-items:center;"
            "padding:11px 16px;border-bottom:1px solid #F0EDE8;font-size:14px}"
            ".row:last-child{border-bottom:none}"
            ".row .lbl{color:#5C5752}.row .val{color:#2C2825;font-weight:500;text-align:right}"
            ".badge{display:inline-block;padding:2px 10px;border-radius:20px;font-size:12px;font-weight:600}"
            ".badge.ok{background:#E8F5E9;color:#2E7D32}"
            ".badge.warn{background:#FFF3E0;color:#E65100}"
            "</style></head><body><div class='card'>"
            "<h1 style='margin-bottom:20px'>Information</h1>";

    html += "<div class='sec'><div class='sec-hd'>DEVICE</div>";
    html += "<div class='row'><span class='lbl'>Device Type</span><span class='val'>";
    html += device_info::kDeviceType;
    html += "</span></div>";
    html += "<div class='row'><span class='lbl'>MAC Address</span><span class='val'>..." +
            mac.substring(mac.length() - 8) + "</span></div>";
    html += "<div class='row'><span class='lbl'>Device Key</span><span class='val'>..." +
            deviceKey.substring(deviceKey.length() - 8) + "</span></div>";
    html += "<div class='row'><span class='lbl'>Firmware Version</span><span class='val'>";
    html += device_info::kFirmwareVersion;
    html += "</span></div>";
    html += "</div>";

    html += "<div class='sec'><div class='sec-hd'>POWER</div>";
    char vbuf[12];
    snprintf(vbuf, sizeof(vbuf), "%.2f V", bat.voltage_mv / 1000.0f);
    html += "<div class='row'><span class='lbl'>Voltage</span><span class='val'>" + String(vbuf) + "</span></div>";
    html += "<div class='row'><span class='lbl'>Percentage</span><span class='val'>" + String(bat.percent) +
            "%</span></div>";
    html += "<div class='row'><span class='lbl'>Status</span><span class='val'>";
    if (bat.charging) html += "<span class='badge ok'>Charging</span>";
    else html += "<span class='badge warn'>Not Charging</span>";
    html += "</span></div>";
    html += "<div class='row'><span class='lbl'>Data Source</span><span class='val'>ADC (resistor divider)</span></div>";
    html += "</div>";

    html += "<div class='sec'><div class='sec-hd'>NETWORK</div>";
    html += "<div class='row'><span class='lbl'>Status</span><span class='val'>";
    if (conn) html += "<span class='badge ok'>Connected</span>";
    else html += "<span class='badge warn'>Disconnected</span>";
    html += "</span></div>";
    if (conn) {
        html += "<div class='row'><span class='lbl'>WiFi</span><span class='val'>" + WiFi.SSID() + "</span></div>";
        html += "<div class='row'><span class='lbl'>IP Address</span><span class='val'>" +
                WiFi.localIP().toString() + "</span></div>";
        html += "<div class='row'><span class='lbl'>Signal (RSSI)</span><span class='val'>" +
                String(WiFi.RSSI()) + " dBm</span></div>";
    }
    html += "</div>";

    html += "<a class='back' href='/portal'>Portal</a></div></body></html>";

    request->send(200, "text/html", html);
}

// ============================================================
// GET /logs + GET /logs.raw — live device log viewer. RAM ring buffer
// only (src/remote_log.h) — resets on reboot, nothing persisted to SD.
// ============================================================

void handleLogsPage(AsyncWebServerRequest *request) {
    String html;
    html.reserve(2600);
    html = "<!DOCTYPE html><html lang='en'><head>"
           "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Device Logs - Fraimic</title><style>";
    html += CSS;
    html += ".pg{padding:0}"
            "#logbox{background:#1E1C19;color:#D8D4CC;border-radius:12px;padding:14px;"
            "font-family:ui-monospace,Menlo,Consolas,monospace;font-size:12px;line-height:1.5;"
            "height:52vh;overflow-y:auto;white-space:pre-wrap;word-break:break-word}"
            ".hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}"
            ".hdr h1{margin-bottom:0}"
            ".pill{font-size:12px;color:#8C8882;background:#F6F4F0;border-radius:20px;padding:4px 10px}"
            "</style></head><body><div class='card' style='max-width:640px'>"
            "<div class='hdr'><h1>Device Logs</h1><span class='pill' id='pill'>live</span></div>"
            "<div id='logbox'>Loading\\u2026</div>"
            "<div class='hint'>Auto-refreshes every second. Scroll up to pause auto-scroll. "
            "Resets on reboot &mdash; only main.cpp's boot-sequence log lines are captured here, "
            "not every subsystem's Serial output.</div>"
            "<a class='back' href='/portal'>Portal</a>"
            "</div>"
            "<script>"
            "var box=document.getElementById('logbox'),pill=document.getElementById('pill');"
            "function nearBottom(){return box.scrollHeight-box.scrollTop-box.clientHeight<40;}"
            "function poll(){"
            "var stick=nearBottom();"
            "fetch('/logs.raw',{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){"
            "box.textContent=t;"
            "if(stick)box.scrollTop=box.scrollHeight;"
            "pill.textContent='live';pill.style.color='#4CAF50';"
            "}).catch(function(){pill.textContent='offline';pill.style.color='#E53935';});"
            "}"
            "poll();setInterval(poll,1000);"
            "</script></body></html>";

    request->send(200, "text/html", html);
}

void handleLogsRaw(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", Log.snapshot());
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

// ============================================================
// GET /ota + POST /update — browser firmware update. Works out of the
// box: partitions.csv already defines dual ota_0/ota_1 app slots.
// ============================================================

void handleOtaPage(AsyncWebServerRequest *request) {
    String html;
    html.reserve(3200);
    html = "<!DOCTYPE html><html lang='en'><head>"
           "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Firmware Update - Fraimic</title><style>";
    html += CSS;
    html += ".drop{border:2px dashed #DDD8D0;border-radius:12px;padding:22px 16px;text-align:center;"
            "cursor:pointer;transition:border .2s,background .2s}"
            ".drop:hover,.drop.over{border-color:#B8964A;background:#FBF9F5}"
            ".drop-label{font-size:14px;color:#5C5752;pointer-events:none}"
            ".drop-sub{font-size:12px;color:#9C9490;margin-top:4px;pointer-events:none}"
            "#prog{text-align:center;font-size:13px;margin-top:12px;min-height:18px}"
            ".bar{width:100%;height:8px;border-radius:4px;background:#F0EDE8;overflow:hidden;margin-top:10px;display:none}"
            ".bar-fill{height:100%;width:0%;background:#B8964A;transition:width .15s}"
            "</style></head><body><div class='card'>"
            "<div class='ic'>";
    html += SVG_CHIP;
    html += "</div><h1>Firmware Update</h1>"
            "<p class='sub'>Upload a compiled firmware .bin (pio run, then .pio/build/&hellip;/firmware.bin)</p>"
            "<form id='uf'>"
            "<div class='group'>"
            "<label>Select Firmware File</label>"
            "<div class='drop' id='drop' onclick='document.getElementById(\"fi\").click()'>"
            "<div class='drop-label' id='dropLabel'>Click to choose a .bin file</div>"
            "<div class='drop-sub' id='dropSub'>Compiled firmware image (.bin)</div>"
            "</div>"
            "<input type='file' id='fi' name='firmware' accept='.bin' style='display:none'>"
            "</div>"
            "<div class='bar' id='bar'><div class='bar-fill' id='barFill'></div></div>"
            "<button type='submit' class='btn' id='upBtn'>Flash Firmware</button>"
            "</form>"
            "<div id='prog'></div>"
            "<div class='hint'>Device will reboot automatically once the update finishes. Don't close this page or lose power during the update.</div>"
            "<a class='back' href='/portal'>Portal</a>"
            "</div>"
            "<script>"
            "document.getElementById('fi').addEventListener('change',function(){"
            "var f=this.files[0];"
            "if(f){"
            "document.getElementById('dropLabel').textContent=f.name;"
            "document.getElementById('dropSub').textContent=(f.size/1024).toFixed(1)+'KB';"
            "}"
            "});"
            "document.getElementById('uf').addEventListener('submit',function(e){"
            "e.preventDefault();"
            "var file=document.getElementById('fi').files[0];"
            "if(!file){alert('Please select a firmware .bin file');return;}"
            "if(!confirm('Flash this firmware and reboot the device?'))return;"
            "var btn=document.getElementById('upBtn'),prog=document.getElementById('prog');"
            "var bar=document.getElementById('bar'),fill=document.getElementById('barFill');"
            "btn.disabled=true;btn.textContent='Flashing...';bar.style.display='block';"
            "prog.style.color='#B8964A';prog.textContent='Uploading firmware\\u2026';"
            "var fd=new FormData();fd.append('firmware',file);"
            "var xhr=new XMLHttpRequest();"
            "xhr.open('POST','/update',true);"
            "xhr.upload.onprogress=function(ev){"
            "if(ev.lengthComputable){"
            "var pct=Math.round(ev.loaded/ev.total*100);"
            "fill.style.width=pct+'%';"
            "prog.textContent='Uploading\\u2026 '+pct+'%';"
            "}"
            "};"
            "xhr.onload=function(){"
            "if(xhr.status===200){"
            "fill.style.width='100%';"
            "prog.style.color='#4CAF50';prog.textContent='Update successful \\u2014 rebooting\\u2026';"
            "}else{"
            "prog.style.color='#E53935';prog.textContent='Update failed: '+xhr.status;"
            "btn.disabled=false;btn.textContent='Flash Firmware';"
            "}"
            "};"
            "xhr.onerror=function(){"
            "prog.style.color='#E53935';prog.textContent='Connection lost (device may be rebooting)';"
            "};"
            "xhr.send(fd);"
            "});"
            "</script></body></html>";

    request->send(200, "text/html", html);
}

void handleOtaFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len,
                    bool final) {
    (void)request;
    if (index == 0) {
        Log.printf("OTA: start (%s)\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Log);
    }
    if (len) {
        if (Update.write(data, len) != len) Update.printError(Log);
    }
    if (final) {
        if (Update.end(true)) Log.printf("OTA: success, %u bytes\n", (unsigned)(index + len));
        else Update.printError(Log);
    }
}

void restartSoon() {
    xTaskCreate(
        [](void *) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        },
        "ota_restart", 2048, nullptr, 1, nullptr);
}

void handleOtaDone(AsyncWebServerRequest *request) {
    bool ok = !Update.hasError();
    AsyncWebServerResponse *response =
        request->beginResponse(ok ? 200 : 500, "text/plain", ok ? "OK. Rebooting..." : "Update failed");
    response->addHeader("Connection", "close");
    request->send(response);
    if (ok) restartSoon();
}

// POST /beep — speaker smoke test, not part of any Fraimic-compatible
// behavior. Blocks the async worker task for the tone's duration, so keep
// it short; fine for an occasional manual trigger.
void handleBeep(AsyncWebServerRequest *request) {
    audio::beep(1000, 300);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// POST /mic-test — records ~1s and reports amplitude stats instead of just
// a pass/fail, since the ES7210 hasn't been cross-checked on this board
// (see README). All-zero/flat output across repeated calls with the mic
// making noise nearby means it's not really capturing anything even if
// isMicReady() came back true. Blocks the async worker task for ~1s.
void handleMicTest(AsyncWebServerRequest *request) {
    const size_t kSamples = audio::kSampleRateHz;  // ~1 second
    int16_t *buf = (int16_t *)malloc(kSamples * sizeof(int16_t));
    if (!buf) {
        request->send(500, "application/json", "{\"error\":\"no memory\"}");
        return;
    }

    size_t got = audio::recordPcm(buf, kSamples);

    int16_t minV = 0, maxV = 0;
    double sumSq = 0;
    for (size_t i = 0; i < got; i++) {
        if (i == 0 || buf[i] < minV) minV = buf[i];
        if (i == 0 || buf[i] > maxV) maxV = buf[i];
        sumSq += (double)buf[i] * buf[i];
    }
    double rms = got ? sqrt(sumSq / got) : 0;

    char json[192];
    snprintf(json, sizeof(json),
             "{\"mic_ready\":%s,\"samples_requested\":%u,\"samples_received\":%u,"
             "\"min\":%d,\"max\":%d,\"rms\":%.1f}",
             audio::isMicReady() ? "true" : "false", (unsigned)kSamples, (unsigned)got, minV, maxV, rms);
    free(buf);

    request->send(200, "application/json", json);
}

}  // namespace

void begin(AsyncWebServer &server) {
    uploadImageBuf = (uint8_t *)heap_caps_malloc(EPD_13IN3E_FRAIMIC_BIN_BYTES, MALLOC_CAP_SPIRAM);

    server.on("/", HTTP_GET, handlePortal);
    server.on("/portal", HTTP_GET, handlePortal);

    server.on("/wifi", HTTP_GET, handleWifiPage);
    server.on("/wifi/scan.json", HTTP_GET, handleWifiScanJson);
    server.on("/wifi/save", HTTP_POST, handleWifiSave);

    server.on("/upload", HTTP_GET, handleUploadPage);
    server.on("/upload", HTTP_POST, handleUploadDone, handleUploadFile);

    server.on("/info", HTTP_GET, handleInfoPage);

    server.on("/logs", HTTP_GET, handleLogsPage);
    server.on("/logs.raw", HTTP_GET, handleLogsRaw);

    server.on("/ota", HTTP_GET, handleOtaPage);
    server.on("/update", HTTP_POST, handleOtaDone, handleOtaFile);

    server.on("/beep", HTTP_POST, handleBeep);
    server.on("/mic-test", HTTP_POST, handleMicTest);
}

}  // namespace web_portal
