#include "web_server.h"

#include <stdlib.h>
#include <string.h>

#include "ble_presence.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "nvs_flash.h"
#include "ota_service.h"
#include "power_service.h"
#include "wifi_service.h"
#include "zigbee_service.h"

static const char *TAG = "web";
static httpd_handle_t s_server;
static portMUX_TYPE s_events_lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned s_event_clients;

static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>BC250 Controller</title><style>"
":root{color-scheme:dark;font:16px system-ui;background:#10151c;color:#e8eef7}body{max-width:900px;margin:auto;padding:18px}"
"h1{margin:.2em 0}.card{background:#18212c;border:1px solid #2b3b4d;border-radius:12px;padding:16px;margin:14px 0}"
"button,input,select{font:inherit;padding:9px;margin:4px;border-radius:7px;border:1px solid #52677e;background:#111923;color:inherit}"
"button{cursor:pointer;background:#1768ac}button.danger{background:#9d2836}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:8px}"
"label{display:flex;flex-direction:column;color:#aebed0}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}"
"pre{white-space:pre-wrap}.ok{color:#70dc91}.bad{color:#ff7d87}small{color:#9fb1c5}</style></head><body>"
"<h1>BC250 Power Controller</h1><div id=message></div>"
"<section class=card><h2>Status</h2><div id=status>Loading…</div><div class=row>"
"<button onclick=power('on')>Power on</button><button onclick=power('off')>Shut down</button>"
"<button onclick=power('toggle')>Toggle</button><button class=danger onclick=power('force_off')>Force off</button></div></section>"
"<section class=card><h2>Radio and network</h2><div class=grid>"
"<label>Operating profile<select id=radio><option>wifi</option><option>zigbee</option><option>hybrid</option></select></label>"
"<label>Hostname<input id=hostname maxlength=31></label><label>Wi-Fi SSID<input id=ssid maxlength=32></label>"
"<label>New Wi-Fi password<input id=wpass type=password maxlength=64 placeholder='leave blank to keep'></label>"
"<label>Zigbee channel (0 = auto)<input id=zbchannel type=number min=0 max=26></label>"
"<label>Zigbee manufacturer<input id=zbmanufacturer maxlength=32></label><label>Zigbee model<input id=zbmodel maxlength=32></label>"
"<div class=row><button onclick=zigbee('commission')>Join Zigbee network</button><button class=danger onclick=zigbee('reset')>Reset Zigbee network</button></div>"
"<label>BLE scan interval (ms)<input id=bleinterval type=number min=20></label><label>BLE scan window (ms)<input id=blewindow type=number min=20></label>"
"<label>BLE absent timeout (ms)<input id=bleabsent type=number min=1000></label></div></section>"
"<section class=card><h2>Power wiring</h2><p><small>Use -1 for disabled. Safe pins are enforced unless advanced override is selected.</small></p>"
"<div class=grid><label>PS_ON GPIO<input id=pson type=number></label><label>Power button GPIO<input id=pbtn type=number></label>"
"<label>Power LED sense GPIO<input id=sense type=number></label><label>Status LED GPIO<input id=led type=number></label>"
"<label class=row><input id=psonactive type=checkbox>PS_ON active high</label><label class=row><input id=pbtnactive type=checkbox>Button active high</label>"
"<label class=row><input id=senseactive type=checkbox>Sense active high</label><label class=row><input id=ledactive type=checkbox>Status LED active high</label>"
"<label>Start strategy<select id=strategy><option value=0>PS_ON only</option><option value=1>Button only</option><option value=2>PS_ON then button</option><option value=3>Simultaneous</option></select></label>"
"<label>PS_ON to button delay (ms)<input id=delay type=number min=0></label><label>Button pulse (ms)<input id=pulse type=number min=50></label>"
"<label>Handoff delay (ms)<input id=handoff type=number min=0></label><label>Start timeout (ms)<input id=starttimeout type=number min=1000></label>"
"<label>Shutdown timeout (ms)<input id=stoptimeout type=number min=1000></label><label>Force-off hold (ms)<input id=forcehold type=number min=1000></label>"
"<label>Retry cooldown (ms)<input id=cooldown type=number min=0></label><label>Sense-on filter (ms)<input id=senseon type=number min=1></label>"
"<label>Sense-off filter (ms)<input id=senseoff type=number min=1></label></div>"
"<label class=row><input id=advanced type=checkbox> Allow advanced/strapping GPIO choices</label><small class=bad>Warning: advanced pins can prevent boot or pulse an output during reset. Verify your exact board schematic and inactive-state bias first.</small></section>"
"<section class=card><h2>Physical buttons</h2><div id=buttons></div><button onclick=addButton()>Add button</button></section>"
"<section class=card><h2>BLE controllers</h2><div class=row><button onclick=startScan()>Scan for 15 seconds</button><span id=scanstate></span></div>"
"<div id=scanresults></div><h3>Configured</h3><div id=bledevices></div></section>"
"<section class=card id=otasection hidden><h2>Firmware update</h2><p><small>Upload an application .bin for this exact chip and flash layout. Settings are preserved.</small></p><input id=firmware type=file accept=.bin><button onclick=uploadFirmware()>Upload firmware</button></section>"
"<section class=card><button onclick=save()>Save and reboot</button><label class=row>New admin password<input id=admin type=password minlength=8></label><p><small>Full factory reset is available only on the configuration AP and also erases the Zigbee network.</small></p><button class=danger onclick=factoryReset()>Full factory reset</button></section>"
"<script>let cfg={buttons:[],ble_devices:[]};const $=id=>document.getElementById(id);const esc=s=>String(s).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));"
"async function api(url,opt={}){let r=await fetch(url,opt);if(!r.ok)throw Error(await r.text());let t=await r.text();return t?JSON.parse(t):{}}"
"function msg(t,bad=false){$('message').className=bad?'bad':'ok';$('message').textContent=t}"
"async function status(){try{let s=await api('/api/v1/status');$('status').textContent=`Power: ${s.power_state} · sensed: ${s.sensed_on?'on':'off'} · Wi-Fi: ${s.wifi_ip} · Zigbee: ${s.zigbee_joined?'joined':'not joined'}`;$('otasection').hidden=!s.ota_enabled;}catch(e){$('status').textContent=e}}"
"async function power(action){try{await api('/api/v1/power',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action})});msg('Command accepted')}catch(e){msg(e,true)}}"
"async function zigbee(action){if(action==='reset'&&!confirm('Reset the Zigbee network?'))return;try{await api('/api/v1/zigbee',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action})});msg('Zigbee action accepted')}catch(e){msg(e,true)}}"
"async function factoryReset(){if(!confirm('Erase all controller settings and Zigbee network data?'))return;try{await api('/api/v1/factory-reset',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({confirm:'ERASE ALL'})});msg('Factory reset accepted. Rebooting…')}catch(e){msg(e,true)}}"
"async function uploadFirmware(){let file=$('firmware').files[0];if(!file){msg('Choose an application .bin first',true);return}if(!confirm(`Upload ${file.name} and reboot?`))return;try{await api('/api/v1/update',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:file});msg('Firmware accepted. Rebooting…')}catch(e){msg(e,true)}}"
"const actions=['none','on','off','toggle','force_off','config_ap','zigbee_commission','zigbee_reset'];"
"function actionSelect(v){return '<select>'+actions.map(x=>`<option ${x==v?'selected':''}>${x}</option>`).join('')+'</select>'}"
"function renderButtons(){let e=$('buttons');e.innerHTML='';cfg.buttons.forEach((b,i)=>{let d=document.createElement('div');d.className='row';d.innerHTML=`<input type=number value='${b.gpio}' title='GPIO'><label><input type=checkbox ${b.active_high?'checked':''}>active high</label><label><input type=checkbox ${b.pull_up?'checked':''}>pull-up</label><input type=number value='${b.debounce_ms}' title='debounce ms'><input type=number value='${b.double_press_ms}' title='double-press ms'><input type=number value='${b.long_press_ms}' title='long-press ms'>${actionSelect(b.short_action)}${actionSelect(b.double_action)}${actionSelect(b.long_action)}<button class=danger>Remove</button>`;let q=d.querySelectorAll('input,select');q[0].onchange=x=>b.gpio=+x.target.value;q[1].onchange=x=>b.active_high=x.target.checked;q[2].onchange=x=>b.pull_up=x.target.checked;q[3].onchange=x=>b.debounce_ms=+x.target.value;q[4].onchange=x=>b.double_press_ms=+x.target.value;q[5].onchange=x=>b.long_press_ms=+x.target.value;q[6].onchange=x=>b.short_action=x.target.value;q[7].onchange=x=>b.double_action=x.target.value;q[8].onchange=x=>b.long_action=x.target.value;d.querySelector('button').onclick=()=>{cfg.buttons.splice(i,1);renderButtons()};e.appendChild(d)})}"
"function addButton(){cfg.buttons.push({enabled:true,gpio:-1,active_high:false,pull_up:true,debounce_ms:40,double_press_ms:350,long_press_ms:1500,short_action:'toggle',double_action:'none',long_action:'force_off'});renderButtons()}"
"function renderBle(){let e=$('bledevices');e.innerHTML='';cfg.ble_devices.forEach((b,i)=>{let d=document.createElement('div');d.className='row';d.innerHTML=`<input value='${esc(b.label||'')}' placeholder=label><select><option value=0>address</option><option value=1>name exact</option><option value=2>name prefix</option><option value=3>service UUID</option><option value=4>manufacturer data</option></select><input value='${esc(b.value||'')}' placeholder=value><input value='${esc(b.mask||'')}' placeholder=mask><input type=number value='${Number(b.min_rssi ?? -90)}' title='minimum RSSI'><button class=danger>Remove</button>`;let q=d.querySelectorAll('input,select');q[1].value=b.type;q[0].onchange=x=>b.label=x.target.value;q[1].onchange=x=>b.type=+x.target.value;q[2].onchange=x=>b.value=x.target.value;q[3].onchange=x=>b.mask=x.target.value;q[4].onchange=x=>b.min_rssi=+x.target.value;d.querySelector('button').onclick=()=>{cfg.ble_devices.splice(i,1);renderBle()};e.appendChild(d)})}"
"async function startScan(){await api('/api/v1/ble/scan',{method:'POST'});$('scanstate').textContent='Scanning…';setTimeout(loadScan,3000)}"
"async function loadScan(){let a=await api('/api/v1/ble/scan');$('scanresults').innerHTML=a.map(x=>`<div class=row><code>${esc(x.address)}</code> ${esc(x.name||'(unnamed)')} ${Number(x.rssi)} dBm ${x.address_may_rotate?'<small class=bad>Private address may rotate; use stable advertisement data</small>':''}<button data-a='${esc(x.address)}' data-n='${esc(x.name||x.address)}'>Add</button></div>`).join('');$('scanresults').querySelectorAll('button').forEach(b=>b.onclick=()=>{cfg.ble_devices.push({enabled:true,type:0,label:b.dataset.n,value:b.dataset.a,mask:'',min_rssi:-90});renderBle()});if(a.length){$('scanstate').textContent=`${a.length} found`;setTimeout(loadScan,3000)}}"
"async function load(){cfg=await api('/api/v1/config');$('radio').value=cfg.radio_profile;$('hostname').value=cfg.hostname;$('ssid').value=cfg.wifi_ssid;$('zbchannel').value=cfg.zigbee_channel;$('zbmanufacturer').value=cfg.zigbee_manufacturer;$('zbmodel').value=cfg.zigbee_model;$('bleinterval').value=cfg.ble_scan_interval_ms;$('blewindow').value=cfg.ble_scan_window_ms;$('bleabsent').value=cfg.ble_absent_ms;$('advanced').checked=cfg.advanced_gpio_override;$('pson').value=cfg.pins.ps_on.gpio;$('pbtn').value=cfg.pins.power_button.gpio;$('sense').value=cfg.pins.power_sense.gpio;$('led').value=cfg.pins.status_led.gpio;$('psonactive').checked=cfg.pins.ps_on.active_high;$('pbtnactive').checked=cfg.pins.power_button.active_high;$('senseactive').checked=cfg.pins.power_sense.active_high;$('ledactive').checked=cfg.pins.status_led.active_high;$('strategy').value=cfg.timing.strategy;$('delay').value=cfg.timing.inter_output_delay_ms;$('pulse').value=cfg.timing.button_pulse_ms;$('handoff').value=cfg.timing.handoff_delay_ms;$('starttimeout').value=cfg.timing.start_timeout_ms;$('stoptimeout').value=cfg.timing.shutdown_timeout_ms;$('forcehold').value=cfg.timing.force_off_ms;$('cooldown').value=cfg.timing.retry_cooldown_ms;$('senseon').value=cfg.sense_on_ms;$('senseoff').value=cfg.sense_off_ms;renderButtons();renderBle()}"
"async function save(){cfg.configured=true;cfg.radio_profile=$('radio').value;cfg.hostname=$('hostname').value;cfg.wifi_ssid=$('ssid').value;cfg.wifi_password=$('wpass').value;cfg.admin_password=$('admin').value;cfg.zigbee_channel=+$('zbchannel').value;cfg.zigbee_manufacturer=$('zbmanufacturer').value;cfg.zigbee_model=$('zbmodel').value;cfg.ble_scan_interval_ms=+$('bleinterval').value;cfg.ble_scan_window_ms=+$('blewindow').value;cfg.ble_absent_ms=+$('bleabsent').value;cfg.advanced_gpio_override=$('advanced').checked;cfg.pins.ps_on.gpio=+$('pson').value;cfg.pins.power_button.gpio=+$('pbtn').value;cfg.pins.power_sense.gpio=+$('sense').value;cfg.pins.status_led.gpio=+$('led').value;cfg.pins.ps_on.active_high=$('psonactive').checked;cfg.pins.power_button.active_high=$('pbtnactive').checked;cfg.pins.power_sense.active_high=$('senseactive').checked;cfg.pins.status_led.active_high=$('ledactive').checked;cfg.timing.strategy=+$('strategy').value;cfg.timing.inter_output_delay_ms=+$('delay').value;cfg.timing.button_pulse_ms=+$('pulse').value;cfg.timing.handoff_delay_ms=+$('handoff').value;cfg.timing.start_timeout_ms=+$('starttimeout').value;cfg.timing.shutdown_timeout_ms=+$('stoptimeout').value;cfg.timing.force_off_ms=+$('forcehold').value;cfg.timing.retry_cooldown_ms=+$('cooldown').value;cfg.sense_on_ms=+$('senseon').value;cfg.sense_off_ms=+$('senseoff').value;try{await api('/api/v1/config',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});msg('Saved. Rebooting…')}catch(e){msg(e,true)}}"
"load().catch(e=>msg(e,true));status();setInterval(status,3000);let events=new EventSource('/api/v1/events');events.addEventListener('status',()=>status());</script></body></html>";

static bool authorized(httpd_req_t *request)
{
    if (bc250_wifi_is_config_ap()) return true;
    size_t header_length = httpd_req_get_hdr_value_len(request, "Authorization");
    if (header_length < 7 || header_length > 180) return false;
    char header[181];
    if (httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header)) != ESP_OK ||
        strncmp(header, "Basic ", 6) != 0) return false;
    unsigned char decoded[128];
    size_t decoded_length = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_length,
                              (const unsigned char *)&header[6], strlen(&header[6])) != 0) return false;
    decoded[decoded_length] = '\0';
    char *separator = strchr((char *)decoded, ':');
    if (separator == NULL) return false;
    *separator = '\0';
    return strcmp((char *)decoded, "admin") == 0 && bc250_config_password_verify(separator + 1);
}

static bool require_auth(httpd_req_t *request)
{
    if (authorized(request)) return true;
    httpd_resp_set_status(request, "401 Unauthorized");
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"BC250 Controller\"");
    httpd_resp_sendstr(request, "Authentication required");
    return false;
}

static esp_err_t root_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    bc250_power_outputs_t outputs = bc250_power_service_outputs();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", BC250_VERSION);
    cJSON_AddStringToObject(root, "power_state", bc250_power_state_name(bc250_power_service_state()));
    cJSON_AddBoolToObject(root, "sensed_on", bc250_power_service_sensed_on());
    cJSON_AddBoolToObject(root, "ps_on_active", outputs.ps_on);
    cJSON_AddBoolToObject(root, "power_button_active", outputs.power_button);
    cJSON_AddBoolToObject(root, "wifi_connected", bc250_wifi_is_connected());
    cJSON_AddStringToObject(root, "wifi_ip", bc250_wifi_ip_address());
    cJSON_AddBoolToObject(root, "config_ap", bc250_wifi_is_config_ap());
    cJSON_AddBoolToObject(root, "zigbee_started", bc250_zigbee_is_started());
    cJSON_AddBoolToObject(root, "zigbee_joined", bc250_zigbee_is_joined());
#ifdef CONFIG_BC250_OTA_ENABLED
    cJSON_AddBoolToObject(root, "ota_enabled", true);
#else
    cJSON_AddBoolToObject(root, "ota_enabled", false);
#endif
    cJSON *present = cJSON_AddArrayToObject(root, "ble_present");
    const bc250_config_t *config = bc250_config_get();
    for (int i = 0; i < config->ble_device_count; ++i) {
        if (bc250_ble_device_present(i)) cJSON_AddItemToArray(present, cJSON_CreateString(config->ble_devices[i].label));
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return err;
}

static esp_err_t receive_body(httpd_req_t *request, char **body, size_t maximum)
{
    if (request->content_len <= 0 || (size_t)request->content_len > maximum) return ESP_ERR_INVALID_SIZE;
    char *buffer = malloc(request->content_len + 1);
    if (buffer == NULL) return ESP_ERR_NO_MEM;
    int offset = 0;
    while (offset < request->content_len) {
        int received = httpd_req_recv(request, &buffer[offset], request->content_len - offset);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0) {
            free(buffer);
            return ESP_FAIL;
        }
        offset += received;
    }
    buffer[offset] = '\0';
    *body = buffer;
    return ESP_OK;
}

static esp_err_t power_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *body;
    if (receive_body(request, &body, 256) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    cJSON *action = json ? cJSON_GetObjectItemCaseSensitive(json, "action") : NULL;
    bc250_power_action_t command = BC250_POWER_ACTION_NONE;
    if (cJSON_IsString(action)) {
        if (strcmp(action->valuestring, "on") == 0) command = BC250_POWER_ACTION_ON;
        else if (strcmp(action->valuestring, "off") == 0) command = BC250_POWER_ACTION_OFF;
        else if (strcmp(action->valuestring, "toggle") == 0) command = BC250_POWER_ACTION_TOGGLE;
        else if (strcmp(action->valuestring, "force_off") == 0) command = BC250_POWER_ACTION_FORCE_OFF;
    }
    cJSON_Delete(json);
    if (command == BC250_POWER_ACTION_NONE || !bc250_power_service_request(command)) {
        httpd_resp_set_status(request, "409 Conflict");
        httpd_resp_send(request, "Command rejected", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

static esp_err_t config_get_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *json = bc250_config_to_json(bc250_config_get(), false);
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return err;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
}

static esp_err_t config_put_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *body;
    if (receive_body(request, &body, 12288) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Configuration is empty or too large");
        return ESP_FAIL;
    }
    bc250_config_t next = *bc250_config_get();
    char error[160];
    esp_err_t err = bc250_config_patch_json(&next, body, error, sizeof(error));
    free(body);
    if (err == ESP_OK) err = bc250_config_save_pending(&next);
    if (err != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, error[0] ? error : esp_err_to_name(err));
        return err;
    }
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, "{\"accepted\":true,\"rebooting\":true}");
    xTaskCreate(restart_task, "restart", 2048, NULL, 2, NULL);
    return ESP_OK;
}

static esp_err_t ble_scan_post_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    bc250_ble_start_learning(15000);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true,\"duration_ms\":15000}");
}

static esp_err_t ble_scan_get_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *json = bc250_ble_scan_results_json();
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return err;
}

static esp_err_t zigbee_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *body = NULL;
    if (receive_body(request, &body, 128) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid Zigbee action");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    cJSON *action = json ? cJSON_GetObjectItemCaseSensitive(json, "action") : NULL;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(action)) {
        if (strcmp(action->valuestring, "commission") == 0) err = bc250_zigbee_commission();
        else if (strcmp(action->valuestring, "reset") == 0) err = bc250_zigbee_factory_reset();
    }
    cJSON_Delete(json);
    if (err != ESP_OK) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Zigbee action unavailable");
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

static void factory_reset_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK) esp_restart();
    ESP_LOGE(TAG, "Factory reset failed: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
}

static esp_err_t factory_reset_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    if (!bc250_wifi_is_config_ap()) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Open the configuration AP first");
        return ESP_FAIL;
    }
    char *body = NULL;
    if (receive_body(request, &body, 128) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Confirmation required");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    cJSON *confirm = json ? cJSON_GetObjectItemCaseSensitive(json, "confirm") : NULL;
    bool confirmed = cJSON_IsString(confirm) && strcmp(confirm->valuestring, "ERASE ALL") == 0;
    cJSON_Delete(json);
    if (!confirmed) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Confirmation required");
        return ESP_FAIL;
    }
    if (xTaskCreate(factory_reset_task, "factory_reset", 3072, NULL, 2, NULL) != pdPASS) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to start reset");
        return ESP_FAIL;
    }
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_sendstr(request, "{\"accepted\":true,\"rebooting\":true}");
}

static void events_task(void *arg)
{
    httpd_req_t *request = arg;
    httpd_resp_set_type(request, "text/event-stream");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(request, "Connection", "keep-alive");
    bc250_power_state_t previous_state = BC250_POWER_UNKNOWN;
    bool previous_sense = !bc250_power_service_sensed_on();
    int64_t started = esp_timer_get_time();
    int64_t keepalive_at = started;
    while (esp_timer_get_time() - started < 60000000LL) {
        bc250_power_state_t state = bc250_power_service_state();
        bool sensed = bc250_power_service_sensed_on();
        if (state != previous_state || sensed != previous_sense) {
            char event[192];
            snprintf(event, sizeof(event),
                     "event: status\ndata: {\"power_state\":\"%s\",\"sensed_on\":%s}\n\n",
                     bc250_power_state_name(state), sensed ? "true" : "false");
            if (httpd_resp_send_chunk(request, event, HTTPD_RESP_USE_STRLEN) != ESP_OK) break;
            previous_state = state;
            previous_sense = sensed;
            keepalive_at = esp_timer_get_time();
        } else if (esp_timer_get_time() - keepalive_at >= 10000000LL) {
            if (httpd_resp_send_chunk(request, ": keepalive\n\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) break;
            keepalive_at = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    httpd_resp_send_chunk(request, NULL, 0);
    httpd_req_async_handler_complete(request);
    portENTER_CRITICAL(&s_events_lock);
    --s_event_clients;
    portEXIT_CRITICAL(&s_events_lock);
    vTaskDelete(NULL);
}

static esp_err_t events_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    portENTER_CRITICAL(&s_events_lock);
    bool available = s_event_clients < 2;
    if (available) ++s_event_clients;
    portEXIT_CRITICAL(&s_events_lock);
    if (!available) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        httpd_resp_sendstr(request, "Too many event clients");
        return ESP_FAIL;
    }
    httpd_req_t *copy = NULL;
    if (httpd_req_async_handler_begin(request, &copy) != ESP_OK ||
        xTaskCreate(events_task, "web_events", 4096, copy, 3, NULL) != pdPASS) {
        if (copy != NULL) httpd_req_async_handler_complete(copy);
        portENTER_CRITICAL(&s_events_lock);
        --s_event_clients;
        portEXIT_CRITICAL(&s_events_lock);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ota_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    esp_err_t err = bc250_ota_handle_http(request);
    if (err == ESP_OK) xTaskCreate(restart_task, "ota_restart", 2048, NULL, 2, NULL);
    return err;
}

static esp_err_t captive_handler(httpd_req_t *request)
{
    if (!bc250_wifi_is_config_ap()) return root_handler(request);
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    return httpd_resp_send(request, NULL, 0);
}

esp_err_t bc250_web_server_start(void)
{
    if (s_server != NULL) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "HTTP server start");
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/api/v1/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/v1/power", .method = HTTP_POST, .handler = power_handler},
        {.uri = "/api/v1/config", .method = HTTP_GET, .handler = config_get_handler},
        {.uri = "/api/v1/config", .method = HTTP_PUT, .handler = config_put_handler},
        {.uri = "/api/v1/ble/scan", .method = HTTP_POST, .handler = ble_scan_post_handler},
        {.uri = "/api/v1/ble/scan", .method = HTTP_GET, .handler = ble_scan_get_handler},
        {.uri = "/api/v1/zigbee", .method = HTTP_POST, .handler = zigbee_handler},
        {.uri = "/api/v1/factory-reset", .method = HTTP_POST, .handler = factory_reset_handler},
        {.uri = "/api/v1/events", .method = HTTP_GET, .handler = events_handler},
        {.uri = "/api/v1/update", .method = HTTP_POST, .handler = ota_handler},
        {.uri = "/generate_204", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_handler},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(s_server, &handlers[i]));
    }
    ESP_LOGI(TAG, "Web interface started");
    return ESP_OK;
}
