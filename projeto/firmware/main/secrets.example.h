// Copie este arquivo para secrets.h e preencha com os seus dados.
// secrets.h fica fora do git (.gitignore).
#pragma once

// WiFi 2.4 GHz (o ESP32 nao enxerga 5 GHz)
#define WIFI_SSID "SUA-REDE-2.4GHZ"
#define WIFI_PASS "SUA-SENHA"

// Ultimo numero do IP fixo do Zeus: ele pega a rede do roteador por DHCP e
// troca para <rede>.201 (ex.: 192.168.15.201), para o endereco nao mudar.
// Confira se esse numero esta livre na sua rede. 0 = fica no IP do DHCP.
#define ZEUS_IP_FINAL 201

// Cidade para hora e clima (Open-Meteo, sem chave)
#define CIDADE "Sua-Cidade"
#define LAT    "-19.39"
#define LON    "-40.07"

// Ponte no notebook Windows. Na primeira vez, ponte\zeus_ponte.py cria um token
// aleatorio (ponte\zeus_ponte.json) e imprime estas duas linhas prontas para colar.
#define PONTE_URL   "http://192.168.15.10:8770"
#define PONTE_TOKEN "TROQUE-POR-UM-CODIGO-LONGO"

// Endereco da ponte pela internet (Cloudflare). O robo liga para ca e busca os
// comandos do rosto: assim fica online em qualquer rede com internet, mesmo
// quando a ponte nao alcanca o robo (isolamento do roteador, outra rede).
#define ZEUS_NUVEM  "https://zeus.appdevtest.shop"

// Outras redes WiFi 2.4 GHz (opcional). Tambem da para cadastrar pela rede ZEUS:
// sem WiFi conhecido, o robo abre a rede ZEUS (senha 12345678) e a pagina
// http://192.168.4.1 deixa escolher a rede do lugar.
// #define WIFI_SSID2 "OUTRA-REDE"
// #define WIFI_PASS2 "SENHA"
// #define WIFI_SSID3 "CELULAR-ROTEADOR"
// #define WIFI_PASS3 "SENHA"
