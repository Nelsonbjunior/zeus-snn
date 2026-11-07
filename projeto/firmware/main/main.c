// Zeus: robo de bancada com rosto animado no OLED. O ESP32 cuida so do rosto e
// serve a pagina; o celular Android ouve e fala, o cerebro e o Ollama local
// (llama3.1:8b) e o controle do notebook fica na ponte Python do Windows.
// para o hardware do ROBO_IA_IOT (ESP32-S3 + SSD1306, ESP-IDF). Sem LEDs,
// servo, buzzer nem sensores.
//
// Online em qualquer rede: o robo liga para a ponte pela internet (ZEUS_NUVEM,
// rota /robo-canal) e busca os comandos do rosto. A ponte nao precisa alcancar
// o robo, entao isolamento de clientes do roteador, celular no 5 GHz ou o robo
// em outra casa nao derrubam mais o app.
//
// Sem WiFi nenhum (so 5 GHz por perto, portal de evento): no cabo USB do notebook a
// ponte manda os mesmos comandos pela serial (usb_task) e o rosto continua vivo.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "hal/temperature_sensor_ll.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "iomap.h"
#include "screens/i2c_drv.h"
#include "screens/ssd1306_generic.h"
#include "zeus_face.h"
#include "zeus_snn.h"
#include "zeus_cerebro.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "copie main/secrets.example.h para main/secrets.h e preencha"
#endif

#ifndef ZEUS_NUVEM
#define ZEUS_NUVEM "https://zeus.appdevtest.shop"
#endif

#define TAG "zeus"

#define FRAME_MS        40
#define WIFI_TRY_MS     12000           // por rede
#define SPLASH_MS       5000
#define QUERY_MAX       1024
#define AP_SETUP_MS     (3 * 60 * 1000) // aviso "rede ZEUS" no OLED
#define AP_RETRY_MS     30000           // na rede ZEUS sem ninguem: procura o WiFi de novo
#define ABERTAS_MAX     3               // redes abertas testadas por volta (as de melhor sinal)
#define USB_ATIVO_MS    10000           // ponte mandou algo pelo cabo ha menos que isso
#define REDES_MAX       8
#define NVS_REDES       5               // redes cadastradas pela pagina da rede ZEUS
#define SCAN_MAX        20
#define CANAL_ESPERA_MS 35000           // a ponte segura o pedido ate 25 s
#define CANAL_BUF       4096

#define WIFI_OK_BIT     BIT0
#define WIFI_FAIL_BIT   BIT1

static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_sta_netif;
static char s_ip[16] = "0.0.0.0";
static char s_rede[33] = "";           // rede em que esta conectado agora
static bool s_modo_ap;                 // sem WiFi: rede ZEUS com a pagina de configurar
static volatile bool s_canal_ok;       // canal da internet respondendo
static volatile uint32_t s_usb_visto;  // ultimo comando da ponte pelo cabo USB (0 = nunca)

typedef struct { char ssid[33]; char pass[65]; } rede_t;
static rede_t s_redes[REDES_MAX];
static int s_nredes;
static wifi_ap_record_t s_scan[SCAN_MAX];
static uint16_t s_nscan;

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------- WiFi: redes conhecidas ----------------

static void redes_add(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || s_nredes >= REDES_MAX) return;
    for (int i = 0; i < s_nredes; i++)
        if (strcmp(s_redes[i].ssid, ssid) == 0) return;
    strlcpy(s_redes[s_nredes].ssid, ssid, sizeof(s_redes[0].ssid));
    strlcpy(s_redes[s_nredes].pass, pass ? pass : "", sizeof(s_redes[0].pass));
    s_nredes++;
}

// le as redes cadastradas na NVS (mais nova primeiro)
static int nvs_redes_ler(rede_t *out, int max)
{
    nvs_handle_t h;
    int n = 0;
    if (nvs_open("zeus", NVS_READONLY, &h) != ESP_OK) return 0;
    for (int i = 0; i < NVS_REDES && n < max; i++)
    {
        char k[4];
        size_t ls = sizeof(out[n].ssid), lp = sizeof(out[n].pass);
        snprintf(k, sizeof(k), "s%d", i);
        if (nvs_get_str(h, k, out[n].ssid, &ls) != ESP_OK) continue;
        snprintf(k, sizeof(k), "p%d", i);
        if (nvs_get_str(h, k, out[n].pass, &lp) != ESP_OK) out[n].pass[0] = 0;
        n++;
    }
    nvs_close(h);
    return n;
}

static esp_err_t nvs_redes_salvar(const char *ssid, const char *pass)
{
    rede_t lista[NVS_REDES + 1];
    rede_t velhas[NVS_REDES];
    int n = 1, nv = nvs_redes_ler(velhas, NVS_REDES);
    strlcpy(lista[0].ssid, ssid, sizeof(lista[0].ssid));
    strlcpy(lista[0].pass, pass, sizeof(lista[0].pass));
    for (int i = 0; i < nv && n < NVS_REDES; i++)
        if (strcmp(velhas[i].ssid, ssid) != 0) lista[n++] = velhas[i];

    nvs_handle_t h;
    esp_err_t e = nvs_open("zeus", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    for (int i = 0; i < NVS_REDES; i++)
    {
        char k[4];
        snprintf(k, sizeof(k), "s%d", i);
        if (i < n) nvs_set_str(h, k, lista[i].ssid); else nvs_erase_key(h, k);
        snprintf(k, sizeof(k), "p%d", i);
        if (i < n) nvs_set_str(h, k, lista[i].pass); else nvs_erase_key(h, k);
    }
    e = nvs_commit(h);
    nvs_close(h);
    return e;
}

static void redes_carregar(void)
{
    rede_t nv[NVS_REDES];
    int n = nvs_redes_ler(nv, NVS_REDES);
    for (int i = 0; i < n; i++) redes_add(nv[i].ssid, nv[i].pass);
    redes_add(WIFI_SSID, WIFI_PASS);
#ifdef WIFI_SSID2
    redes_add(WIFI_SSID2, WIFI_PASS2);
#endif
#ifdef WIFI_SSID3
    redes_add(WIFI_SSID3, WIFI_PASS3);
#endif
    for (int i = 0; i < s_nredes; i++) ESP_LOGI(TAG, "rede conhecida: %s", s_redes[i].ssid);
}

// ---------------- WiFi: conexao ----------------

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *d = data;
        // reason 201 = rede nao encontrada (tipico de 5 GHz), 15/204 = senha recusada
        ESP_LOGW(TAG, "WiFi caiu (reason %d)", d->reason);
        s_rede[0] = 0;
        xEventGroupClearBits(s_wifi_events, WIFI_OK_BIT);
        xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_OK_BIT);
    }
}

// Mesmo truque do Cozmo, so na rede de casa: pega a rede por DHCP e fixa
// <rede>.ZEUS_IP_FINAL, para o endereco do robo nao mudar a cada boot.
static void fix_ip(void)
{
#if ZEUS_IP_FINAL > 0 && ZEUS_IP_FINAL < 255
    if (strcmp(s_rede, WIFI_SSID) != 0) return;     // rede de fora: fica no DHCP
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_sta_netif, &info) != ESP_OK) return;
    esp_netif_dns_info_t dns;
    bool tem_dns = esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK;
    uint32_t mask = info.netmask.addr;
    info.ip.addr = (info.gw.addr & mask) | (htonl(ZEUS_IP_FINAL) & ~mask);
    esp_netif_dhcpc_stop(s_sta_netif);
    if (esp_netif_set_ip_info(s_sta_netif, &info) == ESP_OK)
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&info.ip));
    // sem DHCP o DNS precisa continuar valendo: o canal da internet depende dele
    if (tem_dns && dns.ip.u_addr.ip4.addr) esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dns_info_t reserva = { .ip = { .type = ESP_IPADDR_TYPE_V4 } };
    reserva.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
    esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_BACKUP, &reserva);
#endif
}

static void scan(void)
{
    s_nscan = SCAN_MAX;
    if (esp_wifi_scan_start(NULL, true) != ESP_OK || esp_wifi_scan_get_ap_records(&s_nscan, s_scan) != ESP_OK)
        s_nscan = 0;
}

static int sinal(const char *ssid)
{
    int best = -1000;
    for (int i = 0; i < s_nscan; i++)
        if (strcmp((char *)s_scan[i].ssid, ssid) == 0 && s_scan[i].rssi > best) best = s_scan[i].rssi;
    return best;
}

static const char *seguranca(wifi_auth_mode_t m)
{
    switch (m)
    {
    case WIFI_AUTH_OPEN: return "aberta";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: case WIFI_AUTH_WPA_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    case WIFI_AUTH_OWE: return "OWE";
    default: return (m == WIFI_AUTH_WPA2_ENTERPRISE || m == WIFI_AUTH_WPA3_ENTERPRISE) ? "empresa (usuario)" : "outra";
    }
}

// seguranca que o scan viu para essa rede (-1 = nao vista)
static int seguranca_vista(const char *ssid)
{
    for (int i = 0; i < s_nscan; i++)
        if (strcmp((char *)s_scan[i].ssid, ssid) == 0) return s_scan[i].authmode;
    return -1;
}

static void desligar(void)
{
    esp_wifi_disconnect();
    xEventGroupWaitBits(s_wifi_events, WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
}

static int tentar(const rede_t *r)
{
    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, r->ssid, sizeof(sta.sta.ssid));
    // rede que o scan viu aberta entra sem senha, mesmo cadastrada com uma
    bool aberta = !r->pass[0] || seguranca_vista(r->ssid) == WIFI_AUTH_OPEN;
    if (!aberta) strlcpy((char *)sta.sta.password, r->pass, sizeof(sta.sta.password));
    // aceita qualquer seguranca com senha (WEP, WPA, WPA2, WPA3): sem isto o IDF exige
    // WPA2 ou melhor quando ha senha e recusa a rede (reason 210)
    sta.sta.threshold.authmode = aberta ? WIFI_AUTH_OPEN : WIFI_AUTH_WEP;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_netif_dhcpc_start(s_sta_netif);               // fix_ip pode ter parado o DHCP
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    xEventGroupClearBits(s_wifi_events, WIFI_OK_BIT | WIFI_FAIL_BIT);
    ESP_LOGI(TAG, "conectando em %s...", r->ssid);
    esp_wifi_connect();
    EventBits_t b = xEventGroupWaitBits(s_wifi_events, WIFI_OK_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
                                        pdMS_TO_TICKS(WIFI_TRY_MS));
    if (b & WIFI_OK_BIT)
    {
        strlcpy(s_rede, r->ssid, sizeof(s_rede));
        fix_ip();
        ESP_LOGI(TAG, "conectado em %s, IP %s", s_rede, s_ip);
        return 1;
    }
    desligar();
    return 0;
}

// Internet de verdade: um portal de evento (pagina de login) nao consegue responder
// por HTTPS com o certificado do ZEUS_NUVEM. Qualquer resposta HTTP serve.
static bool tem_internet(void)
{
    esp_http_client_config_t cfg = {
        .url = ZEUS_NUVEM "/ping",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_err_t e = esp_http_client_perform(c);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    return e == ESP_OK && st > 0;
}

static bool conhecida(const char *ssid)
{
    for (int i = 0; i < s_nredes; i++)
        if (strcmp(s_redes[i].ssid, ssid) == 0) return true;
    return false;
}

// redes abertas (sem senha), da de melhor sinal para a pior; fica so se tiver internet
static int conectar_aberta(void)
{
    int testadas = 0;
    for (int i = 0; i < s_nscan && testadas < ABERTAS_MAX; i++)   // o scan vem do melhor sinal para o pior
    {
        const char *ssid = (char *)s_scan[i].ssid;
        if (s_scan[i].authmode != WIFI_AUTH_OPEN || !ssid[0] || conhecida(ssid)) continue;
        bool repetida = false;
        for (int j = 0; j < i; j++) repetida |= strcmp((char *)s_scan[j].ssid, ssid) == 0;
        if (repetida) continue;
        testadas++;
        rede_t r = { 0 };
        strlcpy(r.ssid, ssid, sizeof(r.ssid));
        if (!tentar(&r)) continue;
        if (tem_internet())
        {
            ESP_LOGI(TAG, "rede aberta %s tem internet: fico nela", ssid);
            return 1;
        }
        ESP_LOGW(TAG, "rede aberta %s sem internet (pagina de login?): proxima", ssid);
        desligar();
    }
    return 0;
}

// procura as redes conhecidas e entra na de melhor sinal (as nao vistas, como rede oculta, por ultimo);
// sem nenhuma, tenta as redes abertas que tenham internet
static int conectar_alguma(void)
{
    esp_wifi_disconnect();
    scan();
    int ordem[REDES_MAX], rssi[REDES_MAX];
    for (int i = 0; i < s_nredes; i++)
    {
        ordem[i] = i;
        rssi[i] = sinal(s_redes[i].ssid);
        // a rede principal (hotspot do PC) ganha de qualquer outra que esteja a vista
        if (rssi[i] > -1000 && strcmp(s_redes[i].ssid, WIFI_SSID) == 0) rssi[i] += 100;
    }
    for (int i = 1; i < s_nredes; i++)
        for (int j = i; j > 0 && rssi[ordem[j]] > rssi[ordem[j - 1]]; j--)
        { int t = ordem[j]; ordem[j] = ordem[j - 1]; ordem[j - 1] = t; }
    for (int i = 0; i < s_nredes; i++)
        if (tentar(&s_redes[ordem[i]])) return 1;

    ESP_LOGW(TAG, "nenhuma rede conhecida respondeu. Redes 2.4 GHz que o ESP32 enxerga daqui:");
    for (int i = 0; i < s_nscan; i++)
        ESP_LOGW(TAG, "  %-32s %4d dBm  canal %2d  %s", (char *)s_scan[i].ssid, s_scan[i].rssi, s_scan[i].primary,
                 seguranca(s_scan[i].authmode));
    if (s_nscan == 0) ESP_LOGW(TAG, "  (nenhuma: celular roteador em 5 GHz? troque para 2,4 GHz)");
    for (int i = 0; i < s_nredes; i++)
        if (sinal(s_redes[i].ssid) == -1000)
            ESP_LOGW(TAG, "  %s nao aparece: desligada ou so em 5 GHz (o ESP32 so enxerga 2,4 GHz)", s_redes[i].ssid);
    return conectar_aberta();
}

// caiu depois de conectado (roteador reiniciou, sinal fraco): tenta para sempre, sem abrir a rede ZEUS
static void wifi_task(void *arg)
{
    for (;;)
    {
        xEventGroupWaitBits(s_wifi_events, WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        ESP_LOGW(TAG, "procurando WiFi de novo");
        while (!conectar_alguma()) vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void canal_task(void *arg);

// Na rede ZEUS: enquanto ninguem estiver configurando pela pagina, procura o WiFi
// de novo de tempos em tempos, sem reiniciar (o rosto segue vivo pelo cabo USB).
static void ap_task(void *arg)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(AP_RETRY_MS));
        wifi_sta_list_t l;
        if (esp_wifi_ap_get_sta_list(&l) == ESP_OK && l.num > 0) continue;
        ESP_LOGW(TAG, "rede ZEUS sem ninguem: procurando WiFi de novo");
        if (!conectar_alguma()) continue;
        s_modo_ap = false;
        esp_wifi_set_mode(WIFI_MODE_STA);            // desliga a rede ZEUS
        xSemaphoreTake(s_lock, portMAX_DELAY);
        face_splash("abra no celular:", s_ip, SPLASH_MS, now_ms());
        xSemaphoreGive(s_lock);
        xTaskCreate(wifi_task, "wifi", 4096, NULL, 4, NULL);
        xTaskCreate(canal_task, "canal", 8192, NULL, 4, NULL);
        vTaskDelete(NULL);
    }
}

static int wifi_start(void)
{
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, NULL));

    redes_carregar();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);                 // resposta rapida ao celular

    for (int volta = 0; volta < 2; volta++)
    {
        if (conectar_alguma())
        {
            xTaskCreate(wifi_task, "wifi", 4096, NULL, 4, NULL);
            return 1;
        }
    }

    // Sem rede: vira ponto de acesso "ZEUS" com a pagina para cadastrar um WiFi
    ESP_LOGW(TAG, "abrindo AP ZEUS (configure o WiFi em http://192.168.4.1)");
    s_modo_ap = true;
    esp_wifi_stop();
    esp_netif_create_default_wifi_ap();
    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, "ZEUS", sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, "12345678", sizeof(ap.ap.password));
    ap.ap.ssid_len = 4;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_APSTA);              // STA ligado para continuar procurando o WiFi
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();
    strlcpy(s_ip, "192.168.4.1", sizeof(s_ip));
    xTaskCreate(ap_task, "ap", 3072, NULL, 3, NULL);
    return 0;
}

// ---------------- HTTP: utilitarios ----------------

static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++)
    {
        if (*s == '+') *o++ = ' ';
        else if (*s == '%' && s[1] && s[2])
        {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 2;
        }
        else *o++ = *s;
    }
    *o = 0;
}

static void url_encode(const char *s, char *out, size_t max)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (; *s && n + 4 < max; s++)
    {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.~", c))
            out[n++] = (char)c;
        else { out[n++] = '%'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15]; }
    }
    out[n] = 0;
}

// Le a query inteira (alocada) - devolve NULL se nao tiver
static char *get_query(httpd_req_t *req)
{
    size_t len = httpd_req_get_url_query_len(req);
    if (len == 0) return NULL;
    if (len >= QUERY_MAX) len = QUERY_MAX - 1;
    char *q = malloc(len + 1);
    if (q && httpd_req_get_url_query_str(req, q, len + 1) != ESP_OK && q[0] == 0)
    {
        free(q);
        return NULL;
    }
    return q;
}

static int arg(const char *q, const char *key, char *out, size_t max)
{
    out[0] = 0;
    if (!q) return 0;
    esp_err_t e = httpd_query_key_value(q, key, out, max);
    if (e != ESP_OK && e != ESP_ERR_HTTPD_RESULT_TRUNC) return 0;   // texto longo: fica o comeco
    url_decode(out);
    return 1;
}

static int arg_int(const char *q, const char *key, int def)
{
    char v[16];
    return arg(q, key, v, sizeof(v)) && v[0] ? atoi(v) : def;
}

static esp_err_t send_text(httpd_req_t *req, const char *s)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, s);
}

static esp_err_t send_json(httpd_req_t *req, const char *s)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, s);
}

// copia s para out escapando para dentro de uma string JSON
static void json_escape(const char *s, char *out, size_t max)
{
    size_t n = 0;
    for (; *s && n + 7 < max; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[n++] = '\\'; out[n++] = (char)c; }
        else if (c < 0x20) n += snprintf(out + n, max - n, "\\u%04x", c);
        else out[n++] = (char)c;
    }
    out[n] = 0;
}

static void html_escape(const char *s, char *out, size_t max)
{
    size_t n = 0;
    for (; *s && n + 7 < max; s++)
    {
        const char *r = *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '&' ? "&amp;" : *s == '"' ? "&quot;" : NULL;
        if (r) n += snprintf(out + n, max - n, "%s", r);
        else out[n++] = *s;
    }
    out[n] = 0;
}

static esp_err_t send_state(httpd_req_t *req)
{
    char buf[96];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(buf, sizeof(buf), "{\"mood\":\"%s\",\"board\":%d}",
             face_mood_name(), face_board_active(now_ms()));
    xSemaphoreGive(s_lock);
    return send_json(req, buf);
}

// Espelha exatamente o framebuffer atual do OLED para o visualizador Windows.
static esp_err_t h_frame(httpd_req_t *req)
{
    uint8_t pixels[128 * 64 / 8];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ssd1306_CopyBuffer(pixels, sizeof(pixels));
    xSemaphoreGive(s_lock);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)pixels, sizeof(pixels));
}

// ---------------- comandos do rosto (rede de casa ou canal da internet) ----------------

// m = humor; w=1 troca "fraca" (ouvindo/pensando); hold = ms para segurar
static void cmd_mood(const char *q)
{
    int m = arg_int(q, "m", 0);
    char w[4];
    int weak = arg(q, "w", w, sizeof(w)) && w[0] == '1';
    int hold = arg_int(q, "hold", 0);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    face_mood_cmd(m, weak, hold > 0 ? (uint32_t)hold : 0, now_ms());
    xSemaphoreGive(s_lock);
    if (!weak) cerebro_humor_pedido(m, hold > 0 ? (uint32_t)hold : 0, now_ms());   // o LLM tambem ensina a rede
}

// o agente manda texto + cara para o display
static void cmd_show(const char *q)
{
    char t[200];
    arg(q, "t", t, sizeof(t));
    int m = arg_int(q, "m", 1);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    face_show_text(t, m, now_ms());
    xSemaphoreGive(s_lock);
    cerebro_texto(now_ms());
}

// o celular avisa enquanto a voz esta tocando (p=1: comecou uma palavra, a boca abre)
static void cmd_talk(const char *q)
{
    int on = arg_int(q, "on", 0);
    int pulse = arg_int(q, "p", 0);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    face_talk(on, pulse, now_ms());
    xSemaphoreGive(s_lock);
}

// agente comecou a responder
static void cmd_spk(const char *q)
{
    (void)q;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    face_speaking(now_ms());
    xSemaphoreGive(s_lock);
}

// tabuleiro: b = 64 casas (ordem FEN), f=1 visto pelas pretas, l = ultimo lance
// "e2e4", t1..t3 = linhas ao lado. off=1 volta para o rosto. Devolve 0 se b invalido.
static int cmd_board(const char *q)
{
    char b[72], l[8], t1[40], t2[40], t3[40];
    int off = arg_int(q, "off", 0);
    arg(q, "b", b, sizeof(b));
    arg(q, "l", l, sizeof(l));
    arg(q, "t1", t1, sizeof(t1));
    arg(q, "t2", t2, sizeof(t2));
    arg(q, "t3", t3, sizeof(t3));
    int flip = arg_int(q, "f", 0);
    if (!off && strlen(b) != 64) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (off) face_board_off();
    else face_set_board(b, flip, l, t1, t2, t3, now_ms());
    xSemaphoreGive(s_lock);
    if (!off) cerebro_tabuleiro(now_ms());
    return 1;
}

// xadrez: on=1 e a vez do Zeus ("calculando..."), n = nome do aparelho adversario
static void cmd_think(const char *q)
{
    char n[40];
    int on = arg_int(q, "on", 0);
    arg(q, "n", n, sizeof(n));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    face_think(on, n, now_ms());
    xSemaphoreGive(s_lock);
}

// teclas apertadas pelo canal/USB (bit k = TECLA[k]); a teclas_task trata como aperto real
static volatile uint32_t s_tecla_virtual;


// uma linha do canal: "/mood?m=1&hold=6000"
static void exec_cmd(char *linha)
{
    char *q = strchr(linha, '?');
    if (q) *q++ = 0;
    if (strcmp(linha, "/mood") == 0) cmd_mood(q);
    else if (strcmp(linha, "/show") == 0) cmd_show(q);
    else if (strcmp(linha, "/talk") == 0) cmd_talk(q);
    else if (strcmp(linha, "/spk") == 0) cmd_spk(q);
    else if (strcmp(linha, "/board") == 0) cmd_board(q);
    else if (strcmp(linha, "/think") == 0) cmd_think(q);
    else if (strcmp(linha, "/usb") == 0) { }        // ponte avisando que o cabo segue ligado
    else if (strcmp(linha, "/tecla") == 0)          // k: 0 Sw1 UP, 1 Sw1 DOWN, 2 Sw2 UP, 3 Sw2 DOWN
    {
        int k = arg_int(q, "k", -1);
        if (k >= 0 && k < 4) __atomic_or_fetch(&s_tecla_virtual, 1u << k, __ATOMIC_RELAXED);
    }
    else if (strcmp(linha, "/snn") == 0)            // s: estimulo (nome ou numero), v: pulso 0..100, c: nivel 0..100 por 30 s
    {
        char nome[16];
        arg(q, "s", nome, sizeof(nome));
        int c = arg_int(q, "c", -1);
        if (!cerebro_estimulo(nome, arg_int(q, "v", c >= 0 ? 0 : 100) / 100.0f, c >= 0 ? c / 100.0f : -1.0f, now_ms()))
            ESP_LOGW(TAG, "snn: estimulo desconhecido %s", nome);
    }
    else if (linha[0]) ESP_LOGW(TAG, "canal: comando desconhecido %s", linha);
}

// ---------------- rede neural (SNN) ----------------

#define SNN_MS          10                  // a rede anda de 10 em 10 ms (10 passos de 1 ms)
#define SNN_GRAVAR_MS   (5 * 60 * 1000)     // aprendeu algo: grava na flash no maximo a cada 5 min
#define SNN_SEMPRE_MS   (30 * 60 * 1000)    // a personalidade muda devagar o tempo todo: grava de 30 em 30 min
#define SNN_NUVEM_MS    (30 * 60 * 1000)    // copia de seguranca do cerebro na ponte (PC)

static volatile bool s_mem_buscar;          // nasceu sem memoria: tenta recuperar a copia da ponte
static volatile bool s_mem_enviar;          // gravou: manda a copia para a ponte
#define SNN_LOG_MS      60000
#define SNN_FOTO_RASTER 128                 // ms de disparos em cada foto para o simulador

static void snn_nvs_carregar(void)
{
    nvs_handle_t h;
    if (nvs_open("cerebro", NVS_READONLY, &h) != ESP_OK)
    {
        ESP_LOGI(TAG, "snn: sem memoria na flash; vai pedir a copia de seguranca da ponte");
        s_mem_buscar = true;
        return;
    }
    size_t n = (size_t)snn_tamanho_memoria();
    uint8_t *buf = malloc(n);
    if (buf && nvs_get_blob(h, "snn", buf, &n) == ESP_OK && snn_carregar(buf, (int)n))
    {
        const snn_leitura_t *L = snn_ler();
        ESP_LOGI(TAG, "snn: memoria carregada (%u recompensas, %u broncas, %u licoes do LLM)",
                 (unsigned)L->recompensas, (unsigned)L->punicoes, (unsigned)L->licoes);
        cerebro_gravado();
    }
    else
    {
        ESP_LOGW(TAG, "snn: sem memoria na flash; vai pedir a copia de seguranca da ponte");
        s_mem_buscar = true;
    }
    free(buf);
    nvs_close(h);
}

static void snn_nvs_gravar(void)
{
    int max = snn_tamanho_memoria();
    uint8_t *buf = malloc(max);
    int n = buf ? snn_salvar(buf, max) : 0;
    nvs_handle_t h;
    if (n <= 0 || nvs_open("cerebro", NVS_READWRITE, &h) != ESP_OK) { free(buf); return; }
    esp_err_t e = nvs_set_blob(h, "snn", buf, (size_t)n);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    free(buf);
    if (e == ESP_OK) { cerebro_gravado(); s_mem_enviar = true; ESP_LOGI(TAG, "snn: memoria gravada (%d bytes)", n); }
    else ESP_LOGW(TAG, "snn: nao gravou: %s", esp_err_to_name(e));
}

static void cerebro_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint32_t t_gravou = now_ms(), t_log = now_ms();
    int64_t gasto_us = 0;
    for (;;)
    {
        uint32_t now = now_ms();
        int64_t t0 = esp_timer_get_time();
        cerebro_passo(now);
        gasto_us += esp_timer_get_time() - t0;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        cerebro_rosto(now);
        xSemaphoreGive(s_lock);
        // aprende o tempo todo: grava logo depois de aprender algo e, de qualquer jeito, de 30 em 30 min
        if ((cerebro_aprendeu() && now - t_gravou >= SNN_GRAVAR_MS) || now - t_gravou >= SNN_SEMPRE_MS)
        {
            snn_nvs_gravar();
            t_gravou = now;
        }
        if (now - t_log >= SNN_LOG_MS)
        {
            const snn_leitura_t *L = snn_ler();
            ESP_LOGI(TAG, "snn: %.1f%% da CPU, oculto %.1f Hz, humor %d, energia %.0f, jeito %s, heap livre %u",
                     gasto_us / 10.0 / (now - t_log), L->taxa, L->humor, L->energia,
                     L->jeito >= 0 ? SNN_NOME_JEITO[L->jeito] : "EQUILIBRADO", (unsigned)esp_get_free_heap_size());
            gasto_us = 0;
            t_log = now;
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SNN_MS));
    }
}

// Foto da rede para o simulador do PC (modo "robo ao vivo"), binario little-endian:
// cabecalho (leituras), potencial de cada neuronio (int8, x100) e os ultimos 128 ms de disparos.
static esp_err_t h_snn(httpd_req_t *req)
{
    char *q = get_query(req);                       // /snn?s=carinho&v=100: estimulo (simulador ao vivo)
    if (q && strstr(q, "s="))
    {
        char linha[QUERY_MAX + 8];
        snprintf(linha, sizeof(linha), "/snn?%s", q);
        free(q);
        exec_cmd(linha);
        return send_text(req, "ok");
    }
    free(q);
    static uint8_t buf[188 + SNN_N];               // o resto (disparos) vai direto da memoria da rede
    const snn_leitura_t *L = snn_ler();
    uint8_t *o = buf;
    uint32_t u = 0x4E4E535Au;                       // "ZSNN"
    uint16_t n = SNN_N, ns = (uint16_t)snn_n_sinapses();
#define POE(x) do { memcpy(o, &(x), sizeof(x)); o += sizeof(x); } while (0)
    POE(u);
    u = L->passo; POE(u);
    POE(n); POE(ns);
    *o++ = (uint8_t)L->humor;
    *o++ = (uint8_t)(int8_t)L->gesto;
    *o++ = (uint8_t)(int8_t)L->jeito;
    *o++ = SNN_FOTO_RASTER;
    u = L->gesto_n; POE(u);
    memcpy(o, L->sens, sizeof(L->sens)); o += sizeof(L->sens);
    memcpy(o, L->emo, sizeof(L->emo)); o += sizeof(L->emo);
    memcpy(o, L->mot, sizeof(L->mot)); o += sizeof(L->mot);
    memcpy(o, L->traco, sizeof(L->traco)); o += sizeof(L->traco);
    POE(L->dopamina); POE(L->energia); POE(L->taxa); POE(L->peso_medio);
    POE(L->disparos); POE(L->recompensas); POE(L->punicoes); POE(L->licoes);
#undef POE
    for (int i = 0; i < SNN_N; i++)
    {
        float v = snn_v(i) * 100.0f;
        *o++ = (uint8_t)(int8_t)(v > 127 ? 127 : v < -128 ? -128 : (int)v);
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send_chunk(req, (const char *)buf, o - buf);
    uint8_t linhas[16 * SNN_BYTES];
    for (int d = SNN_FOTO_RASTER - 1; d >= 0 && e == ESP_OK; d -= 16)   // do mais velho para o atual
    {
        for (int j = 0; j < 16; j++) memcpy(linhas + j * SNN_BYTES, snn_raster(d - j), SNN_BYTES);
        e = httpd_resp_send_chunk(req, (const char *)linhas, sizeof(linhas));
    }
    if (e == ESP_OK) e = httpd_resp_send_chunk(req, NULL, 0);
    return e;
}

// pesos de todas as sinapses (int8: w / 2 * 127), na ordem da rede (a mesma do simulador)
static esp_err_t h_snn_pesos(httpd_req_t *req)
{
    uint8_t buf[512];
    uint16_t ns = (uint16_t)snn_n_sinapses();
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send_chunk(req, (const char *)&ns, 2);
    for (int k0 = 0; k0 < ns && e == ESP_OK; k0 += sizeof(buf))
    {
        int n = ns - k0 < (int)sizeof(buf) ? ns - k0 : (int)sizeof(buf);
        for (int j = 0; j < n; j++)
        {
            float w;
            snn_sinapse(k0 + j, NULL, NULL, &w);
            int q = (int)lroundf(w / 2.0f * 127.0f);
            buf[j] = (uint8_t)(int8_t)(q > 127 ? 127 : q < -127 ? -127 : q);
        }
        e = httpd_resp_send_chunk(req, (const char *)buf, n);
    }
    if (e == ESP_OK) e = httpd_resp_send_chunk(req, NULL, 0);
    return e;
}

// ---------------- HTTP: rotas ----------------

static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    // EMBED_TXTFILES poe um \0 no fim: nao envia
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t h_manifest(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/manifest+json");
    return httpd_resp_sendstr(req,
        "{\"name\":\"Zeus\",\"short_name\":\"Zeus\",\"start_url\":\"/\",\"display\":\"standalone\","
        "\"background_color\":\"#0b0b0b\",\"theme_color\":\"#0b0b0b\",\"icons\":[{\"src\":\"/icon.svg\","
        "\"sizes\":\"any\",\"type\":\"image/svg+xml\"}]}");
}

static esp_err_t h_icon(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/svg+xml");
    return httpd_resp_sendstr(req,
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 96 96'><rect width='96' height='96' rx='20' "
        "fill='#0b0b0b'/><rect x='16' y='30' width='26' height='34' rx='8' fill='#5aa9ff'/>"
        "<rect x='54' y='30' width='26' height='34' rx='8' fill='#5aa9ff'/></svg>");
}

// tudo que a pagina do celular precisa (o cerebro e o Ollama, via ponte do notebook)
static esp_err_t h_cfg(httpd_req_t *req)
{
    static char buf[512];
    char tk[120], br[120], city[80];
    json_escape(PONTE_TOKEN, tk, sizeof(tk));
    json_escape(PONTE_URL, br, sizeof(br));
    json_escape(CIDADE, city, sizeof(city));
    snprintf(buf, sizeof(buf),
             "{\"lat\":\"%s\",\"lon\":\"%s\",\"city\":\"%s\",\"ponte\":\"%s\",\"token\":\"%s\",\"ip\":\"%s\"}",
             LAT, LON, city, br, tk, s_ip);
    return send_json(req, buf);
}

static esp_err_t h_state(httpd_req_t *req)
{
    return send_state(req);
}

static esp_err_t h_mood(httpd_req_t *req)
{
    char *q = get_query(req);
    cmd_mood(q);
    free(q);
    return send_state(req);
}

static esp_err_t h_show(httpd_req_t *req)
{
    char *q = get_query(req);
    cmd_show(q);
    free(q);
    return send_text(req, "ok");
}

static esp_err_t h_talk(httpd_req_t *req)
{
    char *q = get_query(req);
    cmd_talk(q);
    free(q);
    return send_text(req, "ok");
}

static esp_err_t h_spk(httpd_req_t *req)
{
    cmd_spk(NULL);
    return send_text(req, "ok");
}

static esp_err_t h_board(httpd_req_t *req)
{
    char *q = get_query(req);
    int ok = cmd_board(q);
    free(q);
    if (!ok)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_text(req, "b precisa de 64 casas");
    }
    return send_text(req, "ok");
}

static esp_err_t h_think(httpd_req_t *req)
{
    char *q = get_query(req);
    cmd_think(q);
    free(q);
    return send_text(req, "ok");
}

// rede ZEUS: pagina para cadastrar o WiFi do lugar onde o robo esta
static esp_err_t h_wifi_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'><title>Zeus - WiFi</title>"
        "<style>body{margin:0;background:#0b0b0b;color:#eee;font:17px system-ui}main{max-width:460px;margin:auto;padding:20px 16px}"
        "h1{letter-spacing:2px}label{display:block;margin:14px 0 4px;color:#aaa}"
        "select,input,button{width:100%;box-sizing:border-box;padding:13px;border-radius:12px;border:0;font:inherit}"
        "button{margin-top:18px;background:#22c55e;font-weight:800}p{color:#aaa;font-size:15px}</style></head><body><main>"
        "<h1>ZEUS</h1><p>Escolha o WiFi deste lugar (s&oacute; 2,4 GHz). O rob&ocirc; guarda a rede, reinicia e "
        "volta a aparecer no app pela internet.</p><form method='post' action='/wifi'>"
        "<label>Rede</label><select name='s'>");
    char nome[33 * 6], linha[480];
    for (int i = 0; i < s_nscan; i++)
    {
        if (!s_scan[i].ssid[0]) continue;
        html_escape((char *)s_scan[i].ssid, nome, sizeof(nome));
        snprintf(linha, sizeof(linha), "<option value=\"%s\">%s (%d dBm)</option>", nome, nome, s_scan[i].rssi);
        httpd_resp_sendstr_chunk(req, linha);
    }
    httpd_resp_sendstr_chunk(req,
        "<option value=''>outra (digite abaixo)</option></select>"
        "<label>Outra rede (nome exato, se n&atilde;o estiver na lista)</label><input name='o' autocapitalize='off'>"
        "<label>Senha</label><input name='p' type='password'>"
        "<button>Salvar e reiniciar</button></form></main></body></html>");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t h_wifi_salvar(httpd_req_t *req)
{
    char corpo[300] = "", ssid[33], outra[33], pass[65];
    int n = httpd_req_recv(req, corpo, sizeof(corpo) - 1);
    corpo[n > 0 ? n : 0] = 0;
    arg(corpo, "s", ssid, sizeof(ssid));
    arg(corpo, "o", outra, sizeof(outra));
    arg(corpo, "p", pass, sizeof(pass));
    const char *rede = outra[0] ? outra : ssid;
    if (!rede[0])
    {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_text(req, "escolha uma rede");
    }
    esp_err_t e = nvs_redes_salvar(rede, pass);
    ESP_LOGI(TAG, "rede cadastrada: %s (%s)", rede, esp_err_to_name(e));
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
        "<body style='background:#0b0b0b;color:#eee;font:18px system-ui;padding:24px'>"
        "<h2>Rede salva.</h2><p>O Zeus est&aacute; reiniciando. Volte o celular para o WiFi normal e "
        "abra o app: em at&eacute; 1 minuto o rob&ocirc; aparece online.</p>");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 20;
    cfg.lru_purge_enable = true;      // o celular abre varias conexoes; libera as velhas
    cfg.stack_size = 8192;
    httpd_handle_t srv = NULL;
    ESP_ERROR_CHECK(httpd_start(&srv, &cfg));

    const struct { const char *uri; esp_err_t (*fn)(httpd_req_t *); } routes[] = {
        { "/", s_modo_ap ? h_wifi_page : h_root }, { "/manifest.json", h_manifest }, { "/icon.svg", h_icon },
        { "/cfg", h_cfg }, { "/state", h_state }, { "/frame", h_frame }, { "/mood", h_mood }, { "/show", h_show },
        { "/talk", h_talk }, { "/spk", h_spk }, { "/board", h_board }, { "/think", h_think },
        { "/snn", h_snn }, { "/snn/pesos", h_snn_pesos },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
    {
        httpd_uri_t u = { .uri = routes[i].uri, .method = HTTP_GET, .handler = routes[i].fn };
        httpd_register_uri_handler(srv, &u);
    }
    if (s_modo_ap)                    // cadastrar WiFi so pela rede ZEUS (no WiFi de casa nao aparece)
    {
        httpd_uri_t g = { .uri = "/wifi", .method = HTTP_GET, .handler = h_wifi_page };
        httpd_uri_t p = { .uri = "/wifi", .method = HTTP_POST, .handler = h_wifi_salvar };
        httpd_register_uri_handler(srv, &g);
        httpd_register_uri_handler(srv, &p);
    }
}

// ---------------- canal da internet ----------------

typedef struct { char *buf; int n; } rx_t;

static esp_err_t canal_evt(esp_http_client_event_t *e)
{
    rx_t *rx = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && rx)
    {
        int cabe = CANAL_BUF - 1 - rx->n;
        int n = e->data_len < cabe ? e->data_len : cabe;
        if (n > 0) { memcpy(rx->buf + rx->n, e->data, n); rx->n += n; }
    }
    return ESP_OK;
}

// Copia de seguranca do cerebro na ponte: enviar=1 manda a memoria (POST), 0 busca (GET).
// Devolve o codigo HTTP (0 = sem rede).
static int memoria_nuvem(bool enviar)
{
    int max = snn_tamanho_memoria();
    rx_t rx = { .buf = malloc(CANAL_BUF), .n = 0 };
    uint8_t *mem = enviar ? malloc(max) : NULL;
    int n = mem ? snn_salvar(mem, max) : 0;
    if (!rx.buf || (enviar && n <= 0)) { free(rx.buf); free(mem); return 0; }
    esp_http_client_config_t cfg = {
        .url = ZEUS_NUVEM "/robo-memoria",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .method = enviar ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .event_handler = canal_evt,
        .user_data = &rx,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "X-Zeus-Token", PONTE_TOKEN);
    if (enviar)
    {
        esp_http_client_set_header(c, "Content-Type", "application/octet-stream");
        esp_http_client_set_post_field(c, (const char *)mem, n);
    }
    int st = esp_http_client_perform(c) == ESP_OK ? esp_http_client_get_status_code(c) : 0;
    esp_http_client_cleanup(c);
    if (!enviar && st == 200)
    {
        if (snn_carregar((const uint8_t *)rx.buf, rx.n))
        {
            const snn_leitura_t *L = snn_ler();
            ESP_LOGI(TAG, "snn: cerebro recuperado da ponte (%u recompensas, %u licoes)",
                     (unsigned)L->recompensas, (unsigned)L->licoes);
            snn_nvs_gravar();
        }
        else ESP_LOGW(TAG, "snn: copia da ponte nao serve (%d bytes)", rx.n);
    }
    else if (enviar && st == 200) ESP_LOGI(TAG, "snn: copia de seguranca na ponte (%d bytes)", n);
    free(rx.buf);
    free(mem);
    return st;
}

// Busca os comandos na ponte pela internet. A ponte segura o pedido ate ter comando
// (long-poll); a conexao TLS fica aberta entre um pedido e outro (keep-alive).
static void canal_task(void *arg)
{
    rx_t rx = { .buf = malloc(CANAL_BUF), .n = 0 };
    char url[384], mood[16], rede[33 * 3 + 1], resumo[96];
    esp_http_client_handle_t c = NULL;
    int falhas = 0;
    bool avisou = false;
    uint32_t t_nuvem = 0;
    for (;;)
    {
        if (!(xEventGroupGetBits(s_wifi_events) & WIFI_OK_BIT))
        {
            s_canal_ok = false;
            if (c) { esp_http_client_cleanup(c); c = NULL; }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        // copia do cerebro: fecha antes a conexao do canal (uma conexao TLS por vez cabe na RAM)
        bool vez_buscar = s_mem_buscar && s_canal_ok;
        bool vez_enviar = s_mem_enviar && s_canal_ok && (!t_nuvem || now_ms() - t_nuvem >= SNN_NUVEM_MS);
        if ((vez_buscar || vez_enviar) && c) { esp_http_client_cleanup(c); c = NULL; }
        if (vez_buscar)
        {
            int st = memoria_nuvem(false);
            if (st == 200 || st == 404) s_mem_buscar = false;   // 404: a ponte ainda nao tem copia
        }
        if (vez_enviar)
        {
            if (memoria_nuvem(true) == 200) s_mem_enviar = false;
            t_nuvem = now_ms() ? now_ms() : 1;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        strlcpy(mood, face_mood_name(), sizeof(mood));
        int board = face_board_active(now_ms());
        xSemaphoreGive(s_lock);
        url_encode(s_rede, rede, sizeof(rede));
        cerebro_resumo(resumo, sizeof(resumo));
        snprintf(url, sizeof(url), "%s/robo-canal?mood=%s&board=%d&ip=%s&rede=%s&%s",
                 ZEUS_NUVEM, mood, board, s_ip, rede, resumo);
        if (!c)
        {
            esp_http_client_config_t cfg = {
                .url = url,
                .crt_bundle_attach = esp_crt_bundle_attach,
                .timeout_ms = CANAL_ESPERA_MS,
                .keep_alive_enable = true,
                .event_handler = canal_evt,
                .user_data = &rx,
                .buffer_size = 2048,
            };
            c = esp_http_client_init(&cfg);
            esp_http_client_set_header(c, "X-Zeus-Token", PONTE_TOKEN);
        }
        else esp_http_client_set_url(c, url);

        rx.n = 0;
        esp_err_t err = esp_http_client_perform(c);
        int st = esp_http_client_get_status_code(c);
        if (err == ESP_OK && st == 200)
        {
            if (!avisou) { ESP_LOGI(TAG, "online pela internet (%s)", ZEUS_NUVEM); avisou = true; }
            s_canal_ok = true;
            falhas = 0;
            rx.buf[rx.n] = 0;
            for (char *s = rx.buf, *fim; s && *s; s = fim)
            {
                fim = strchr(s, '\n');
                if (fim) *fim++ = 0;
                exec_cmd(s);
            }
            continue;
        }
        avisou = false;
        s_canal_ok = false;
        falhas++;
        ESP_LOGW(TAG, "canal da internet: %s, HTTP %d (tentativa %d)", esp_err_to_name(err), st, falhas);
        esp_http_client_cleanup(c);
        c = NULL;
        // token errado: nao adianta insistir rapido; ponte desligada: tenta de 2 em 2 s ate 15 s
        int espera = st == 403 ? 30000 : (falhas < 8 ? 2000 * falhas : 15000);
        vTaskDelay(pdMS_TO_TICKS(espera));
    }
}

// Cabo USB do notebook (a serial do console, CH340): a ponte manda uma linha por
// comando ("/mood?m=1") e le o estado que o robo imprime a cada segundo:
//   @ZEUS mood=feliz board=0 net=1 ip=10.83.190.201 rede=Ricardo_S23
// net=1 = o canal da internet esta respondendo, e ai a ponte nao usa o cabo.
static void usb_task(void *arg)
{
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    uart_vfs_dev_use_driver(UART_NUM_0);               // os logs continuam saindo pela mesma serial
    char linha[QUERY_MAX], mood[16], rede[33 * 3 + 1];
    int n = 0;
    uint32_t aviso = 0;
    for (;;)
    {
        uint8_t ch;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(200)) == 1)
        {
            if (ch == '\n' || ch == '\r')
            {
                linha[n] = 0;
                if (n && linha[0] == '/')
                {
                    uint32_t agora = now_ms();
                    if (!s_usb_visto || agora - s_usb_visto > USB_ATIVO_MS)
                    {
                        ESP_LOGI(TAG, "ponte ligada pelo cabo USB");
                        xSemaphoreTake(s_lock, portMAX_DELAY);
                        face_splash("", "", 0, agora);      // tira o aviso "rede ZEUS" da frente do rosto
                        xSemaphoreGive(s_lock);
                    }
                    s_usb_visto = agora ? agora : 1;
                    exec_cmd(linha);
                }
                n = 0;
            }
            else if (n < (int)sizeof(linha) - 1) linha[n++] = ch;
            else n = 0;                                     // linha grande demais: descarta
        }
        if (now_ms() - aviso >= 1000)
        {
            aviso = now_ms();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(mood, face_mood_name(), sizeof(mood));
            int board = face_board_active(aviso);
            xSemaphoreGive(s_lock);
            url_encode(s_rede, rede, sizeof(rede));
            char resumo[96];
            cerebro_resumo(resumo, sizeof(resumo));
            for (char *a = resumo; *a; a++) if (*a == '&') *a = ' ';    // campos separados por espaco
            printf("@ZEUS mood=%s board=%d net=%d ip=%s rede=%s %s\n", mood, board, s_canal_ok ? 1 : 0,
                   s_modo_ap ? "" : s_ip, rede, resumo);
            fflush(stdout);
        }
    }
}

// ---------------- Key_Sw1: temperatura do chip / Key_Sw2: carinho e bronca para a rede ----------------

#define TECLA_MS        20      // varredura das teclas
#define TECLA_FIRME     3       // leituras iguais seguidas para aceitar (60 ms, sem repique)
#define TEMP_MS         1000    // nova media na tela a cada segundo
#define TEMP_JANELAS    5       // media movel de 5 s (~250 amostras)
#define TEMP_LOG_MS     5000    // tela da temperatura aberta: temperatura no log serial

enum { SW1_UP, SW1_DOWN, SW2_UP, SW2_DOWN, TECLAS };
static const struct { uint8_t col, lin; const char *nome; } TECLA[TECLAS] = {
    { IOMAP_COLUMN_1, IOMAP_KEY_LINE_2, "Key_Sw1 UP" },
    { IOMAP_COLUMN_1, IOMAP_KEY_LINE_1, "Key_Sw1 DOWN" },
    { IOMAP_COLUMN_2, IOMAP_KEY_LINE_2, "Key_Sw2 UP" },
    { IOMAP_COLUMN_2, IOMAP_KEY_LINE_1, "Key_Sw2 DOWN" },
};

// O driver devolve grau INTEIRO (temperature_sensor_hal_get_degree e int16) menos a calibracao
// do eFuse: anda de 1 em 1 C. O registro do sensor tem passo de 0,4386 C, entao a leitura fina
// refaz a conta do S3 com o valor bruto: T = 0,4386*raw - 27,88*offset - 20,52 - deltaT/10.
// O offset depende da faixa que o driver escolheu: testa os 5 possiveis e fica com o que bate
// com a leitura inteira do driver (eles distam 27,88 C, nao tem como confundir).
static float temp_amostra(temperature_sensor_handle_t ts)
{
    static float cal = -999.0f;
    float grosso;
    if (!ts || temperature_sensor_get_celsius(ts, &grosso) != ESP_OK) return -999.0f;
    if (cal < -100.0f) cal = temperature_sensor_ll_load_calib_param() / 10.0f;
    uint32_t raw = temperature_sensor_ll_get_raw_value();
    float melhor = grosso, erro = 99.0f;
    for (int off = -2; off <= 2; off++)
    {
        float t = 0.4386f * raw - 27.88f * off - 20.52f - cal;
        if (fabsf(t - grosso) < erro) { erro = fabsf(t - grosso); melhor = t; }
    }
    return erro < 2.0f ? melhor : grosso;
}

// Media das amostras (uma a cada varredura de 20 ms, ~50 por segundo): o ruido do sensor
// espalha as leituras entre degraus vizinhos e a media acha o valor entre eles.
typedef struct { double soma, soma2; int n; float min, max; } media_t;

static void media_add(media_t *m, float t)
{
    if (t < -100.0f) return;
    if (!m->n) { m->min = m->max = t; }
    m->soma += t;
    m->soma2 += (double)t * t;
    m->n++;
    if (t < m->min) m->min = t;
    if (t > m->max) m->max = t;
}

static float media_valor(const media_t *m, float *desvio)
{
    if (!m->n) return -999.0f;
    double mu = m->soma / m->n;
    double var = m->soma2 / m->n - mu * mu;
    if (desvio) *desvio = var > 0 ? (float)sqrt(var) : 0.0f;
    return (float)mu;
}

// Le uma coluna da matriz: so ela em nivel alto, as outras como entrada (soltas)
static void ler_coluna(int col, int *up, int *down)
{
    int outra = col == IOMAP_COLUMN_1 ? IOMAP_COLUMN_2 : IOMAP_COLUMN_1;
    gpio_set_direction(outra, GPIO_MODE_INPUT);
    gpio_set_direction(col, GPIO_MODE_OUTPUT);
    gpio_set_level(col, 1);
    esp_rom_delay_us(30);                           // linhas assentarem
    *up = gpio_get_level(IOMAP_KEY_LINE_2);
    *down = gpio_get_level(IOMAP_KEY_LINE_1);
    gpio_set_level(col, 0);
}

// Key_Sw1 UP mostra a temperatura do ESP32, DOWN volta ao rosto.
// Key_Sw2 ensina a rede neural com a mao: UP = carinho (recompensa), DOWN = bronca (punicao).
static void teclas_task(void *arg)
{
    gpio_config_t col = {
        .pin_bit_mask = (1ULL << IOMAP_COLUMN_1) | (1ULL << IOMAP_COLUMN_2),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&col);
    gpio_set_level(IOMAP_COLUMN_1, 0);
    gpio_set_level(IOMAP_COLUMN_2, 0);
    gpio_config_t lin = {
        .pin_bit_mask = (1ULL << IOMAP_KEY_LINE_1) | (1ULL << IOMAP_KEY_LINE_2),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&lin);

    temperature_sensor_handle_t ts = NULL;
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tcfg, &ts) != ESP_OK || temperature_sensor_enable(ts) != ESP_OK)
    {
        ESP_LOGW(TAG, "sensor de temperatura indisponivel");
        ts = NULL;
    }

    int firme[TECLAS] = { 0 }, conta[TECLAS] = { 0 };
    uint32_t t_temp = now_ms(), t_log = 0;
    media_t janela = { 0 };                         // amostras do segundo atual
    media_t ultimos[TEMP_JANELAS] = { 0 };          // os ultimos segundos fechados (media movel)
    int i_janela = 0;
    float c = -999.0f, desvio = 0.0f;               // media movel dos ultimos segundos
    int n_media = 0;
    for (;;)
    {
        int lido[TECLAS], aperto[TECLAS] = { 0 };
        ler_coluna(IOMAP_COLUMN_1, &lido[SW1_UP], &lido[SW1_DOWN]);
        ler_coluna(IOMAP_COLUMN_2, &lido[SW2_UP], &lido[SW2_DOWN]);
        uint32_t virt = __atomic_exchange_n(&s_tecla_virtual, 0, __ATOMIC_RELAXED);
        for (int k = 0; k < TECLAS; k++)
        {
            conta[k] = lido[k] == firme[k] ? 0 : conta[k] + 1;
            if (conta[k] >= TECLA_FIRME) { firme[k] = lido[k]; conta[k] = 0; aperto[k] = lido[k]; }
            if (virt & (1u << k)) aperto[k] = 1;
        }

        uint32_t now = now_ms();
        media_add(&janela, temp_amostra(ts));
        int segundo = now - t_temp >= TEMP_MS;
        if (segundo)
        {
            ultimos[i_janela] = janela;
            i_janela = (i_janela + 1) % TEMP_JANELAS;
            media_t tudo = { 0 };
            for (int i = 0; i < TEMP_JANELAS; i++)
            {
                tudo.soma += ultimos[i].soma;
                tudo.soma2 += ultimos[i].soma2;
                tudo.n += ultimos[i].n;
            }
            c = media_valor(&tudo, &desvio);
            cerebro_temperatura(c);                         // chip quente = fadiga para a rede neural
            n_media = tudo.n;
            janela = (media_t){ 0 };
            t_temp = now;
        }
        int tecla = aperto[SW1_UP] || aperto[SW1_DOWN] || aperto[SW2_UP] || aperto[SW2_DOWN];
        if (aperto[SW1_UP] || aperto[SW1_DOWN]) cerebro_tecla(now);   // a rede sente o toque (cutucao)
        if (aperto[SW2_UP]) cerebro_estimulo("carinho", 1.0f, -1.0f, now);
        if (aperto[SW2_DOWN]) cerebro_estimulo("bronca", 1.0f, -1.0f, now);
        int ler = aperto[SW1_UP] || aperto[SW1_DOWN] || (segundo && face_temp_on());
        if (tecla && c < -100.0f) c = temp_amostra(ts);   // primeiro segundo ainda sem media
        for (int k = 0; k < TECLAS; k++)
            if (aperto[k]) ESP_LOGI(TAG, "%s: temperatura %.2f C", TECLA[k].nome, c);

        if (face_temp_on() && segundo && now - t_log >= TEMP_LOG_MS)
        {
            ESP_LOGI(TAG, "temperatura: %.2f C (desvio %.2f, %d amostras)", c, desvio, n_media);
            t_log = now;
        }

        if (ler)
        {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (aperto[SW1_DOWN]) face_temp(0, c, now);
            else if (aperto[SW1_UP] || face_temp_on()) face_temp(1, c, now);
            xSemaphoreGive(s_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(TECLA_MS));
    }
}

// ---------------- rosto ----------------

static void face_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    for (;;)
    {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        uint32_t now = now_ms();
        face_tick(now);
        face_render(now);
        xSemaphoreGive(s_lock);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(FRAME_MS));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    s_lock = xSemaphoreCreateMutex();
    i2cDrv_init(IOMAP_SDA, IOMAP_SCL, IOMAP_I2C_FREQ, IOMAP_I2C_NUM);
    ssd1306_Init();

    face_init(now_ms());
    cerebro_iniciar(now_ms());
    snn_nvs_carregar();
    ESP_LOGI(TAG, "snn: %d neuronios, %d sinapses", SNN_N, snn_n_sinapses());
    face_splash("conectando...", WIFI_SSID, 60000, now_ms());
    face_render(now_ms());

    int sta = wifi_start();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    face_splash(sta ? "abra no celular:" : "WiFi: rede ZEUS", sta ? s_ip : "192.168.4.1",
                sta ? SPLASH_MS : AP_SETUP_MS, now_ms());
    xSemaphoreGive(s_lock);

    http_start();
    xTaskCreatePinnedToCore(face_task, "face", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(cerebro_task, "cerebro", 4096, NULL, 6, NULL, 1);   // acima do rosto: a rede nao perde tempo
    xTaskCreate(usb_task, "usb", 4096, NULL, 4, NULL);
    xTaskCreate(teclas_task, "teclas", 3072, NULL, 4, NULL);
    if (sta) xTaskCreate(canal_task, "canal", 8192, NULL, 4, NULL);
}
