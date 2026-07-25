#include "wifi_provisioning.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

namespace wifi_provisioning {

namespace {

constexpr const char *kPrefsNamespace = "wifi";
constexpr uint8_t kDnsPort = 53;

DNSServer dnsServer;
WebServer server(80);

String htmlEscape(const String &in) {
    String out;
    out.reserve(in.length());
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

String buildPortalPage() {
    String html =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>E-Paper Frame Setup</title>"
        "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}"
        "select,input{width:100%;padding:.5em;margin:.3em 0 1em;box-sizing:border-box}"
        "button{width:100%;padding:.7em;background:#222;color:#fff;border:0;border-radius:6px}</style>"
        "</head><body><h2>Connect your frame to WiFi</h2><form method='POST' action='/save'>"
        "<label>Network</label><select name='ssid_select' onchange=\"document.getElementById('ssid').value=this.value\">";

    int n = WiFi.scanComplete();
    if (n < 0) n = 0;  // scan still running or not started; page still usable via manual entry
    for (int i = 0; i < n; i++) {
        html += "<option value='" + htmlEscape(WiFi.SSID(i)) + "'>" + htmlEscape(WiFi.SSID(i)) +
                " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }

    html +=
        "</select>"
        "<label>SSID (or pick above)</label><input id='ssid' name='ssid' maxlength='32' required>"
        "<label>Password</label><input name='password' type='password' maxlength='64'>"
        "<button type='submit'>Save &amp; Connect</button>"
        "</form></body></html>";
    return html;
}

void handleRoot() { server.send(200, "text/html", buildPortalPage()); }

void handleSave() {
    if (!server.hasArg("ssid") || server.arg("ssid").isEmpty()) {
        server.send(400, "text/plain", "ssid required");
        return;
    }
    Preferences prefs;
    prefs.begin(kPrefsNamespace, false);
    prefs.putString("ssid", server.arg("ssid"));
    prefs.putString("password", server.arg("password"));
    prefs.end();

    server.send(200, "text/html",
                "<html><body><h3>Saved. Restarting and connecting&hellip;</h3></body></html>");
    delay(1000);
    ESP.restart();
}

// Any unrecognized path (including OS captive-portal probe URLs like
// /generate_204, /hotspot-detect.html) redirects back to the setup page so
// phones/laptops auto-open it.
void handleNotFound() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

}  // namespace

bool connectSaved(uint32_t timeout_ms) {
    Preferences prefs;
    prefs.begin(kPrefsNamespace, true);
    String ssid = prefs.getString("ssid", "");
    String password = prefs.getString("password", "");
    prefs.end();

    if (ssid.isEmpty()) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) {
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

void runProvisioningPortal() {
    WiFi.mode(WIFI_AP);
    uint64_t mac = ESP.getEfuseMac();
    char apName[32];
    snprintf(apName, sizeof(apName), "EPaper-Setup-%04X", (unsigned)(mac & 0xFFFF));
    WiFi.softAP(apName);  // open network — setup-only, short-lived

    WiFi.scanNetworks(true /* async */);  // populate the SSID dropdown in the background

    dnsServer.start(kDnsPort, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    for (;;) {
        dnsServer.processNextRequest();
        server.handleClient();
        delay(2);
    }
}

}  // namespace wifi_provisioning
