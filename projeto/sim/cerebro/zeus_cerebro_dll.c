// Zeus inteiro (rosto + rede neural) numa DLL para a janela do simulador: roda o MESMO
// zeus_face.c, zeus_snn.c e zeus_cerebro.c do firmware, com o OLED num buffer em memoria
// e um relogio que a janela avanca. A janela le os pixels, os neuronios e as sinapses daqui.
#include <stdint.h>
#include <string.h>

#include "oled_port.h"
#include "zeus_face.h"
#include "zeus_snn.h"
#include "zeus_cerebro.h"

#define API __declspec(dllexport)

static uint8_t s_fb[OLED_W * OLED_H / 8];       // mesmo formato do SSD1306 (8 paginas de 128)
static uint32_t s_rng = 12345, s_now = 1000, s_t_quadro;

void oled_clear(void) { memset(s_fb, 0, sizeof(s_fb)); }
void oled_pixel(int x, int y, int on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    uint8_t bit = (uint8_t)(1 << (y & 7));
    if (on) s_fb[(y >> 3) * OLED_W + x] |= bit;
    else s_fb[(y >> 3) * OLED_W + x] &= (uint8_t)~bit;
}
void oled_flush(void) {}
uint32_t oled_rand(void) { s_rng = s_rng * 1103515245u + 12345u; return s_rng >> 8; }

API void zs_iniciar(void)
{
    s_now = 1000;
    face_init(s_now);
    cerebro_iniciar(s_now);
    s_t_quadro = s_now;
    face_render(s_now);
}

// avanca ms de relogio: a rede anda de 10 em 10 ms e o rosto desenha a cada 40 ms, como no robo
API void zs_avancar(int ms)
{
    for (int t = 0; t < ms; t += 10)
    {
        s_now += 10;
        cerebro_passo(s_now);
        cerebro_rosto(s_now);
        if (s_now - s_t_quadro >= 40)
        {
            s_t_quadro = s_now;
            face_tick(s_now);
            face_render(s_now);
        }
    }
}

API uint32_t zs_agora(void) { return s_now; }
API void zs_quadro(uint8_t *out1024) { memcpy(out1024, s_fb, sizeof(s_fb)); }

// os mesmos comandos que o robo recebe da ponte/app
API void zs_humor(int m, int hold_ms)
{
    face_mood_cmd(m, 0, hold_ms > 0 ? (uint32_t)hold_ms : 0, s_now);
    cerebro_humor_pedido(m, hold_ms > 0 ? (uint32_t)hold_ms : 0, s_now);
}
API void zs_texto(const char *utf8, int m) { face_show_text(utf8, m, s_now); cerebro_texto(s_now); }
API void zs_falar(int on) { face_talk(on, 0, s_now); }
API void zs_xadrez_pensa(int on) { face_think(on, "simulador", s_now); }
API int zs_estimulo(const char *nome, float v, float c) { return cerebro_estimulo(nome, v, c, s_now); }
API void zs_tecla(void) { cerebro_tecla(s_now); }
API void zs_temperatura(float c) { cerebro_temperatura(c); }

// leituras da rede
API void zs_leitura(snn_leitura_t *out) { *out = *snn_ler(); }
API int zs_tamanho_leitura(void) { return (int)sizeof(snn_leitura_t); }
API int zs_n(void) { return SNN_N; }
API int zs_n_sinapses(void) { return snn_n_sinapses(); }
API void zs_sinapses(uint16_t *pre, uint16_t *pos, float *w, uint8_t *plastica)
{
    int ns = snn_n_sinapses();
    for (int k = 0; k < ns; k++)
    {
        snn_sinapse(k, &pre[k], &pos[k], &w[k]);
        plastica[k] = (uint8_t)snn_plastica(k);
    }
}
API void zs_potenciais(float *v, float *limiar)
{
    for (int i = 0; i < SNN_N; i++) { v[i] = snn_v(i); limiar[i] = snn_limiar(i); }
}
// disparos dos ultimos n ms (n <= SNN_RASTER), do mais velho para o atual, SNN_BYTES por ms
API int zs_raster(uint8_t *out, int n)
{
    if (n > SNN_RASTER) n = SNN_RASTER;
    for (int d = n - 1, j = 0; d >= 0; d--, j++) memcpy(out + j * SNN_BYTES, snn_raster(d), SNN_BYTES);
    return n;
}
API const char *zs_nome_sensor(int k) { return k >= 0 && k < SNN_SENSORES ? SNN_NOME_SENSOR[k] : ""; }
API const char *zs_nome_emocao(int k) { return k >= 0 && k < SNN_EMOCOES ? SNN_NOME_EMOCAO[k] : ""; }
API const char *zs_nome_motor(int k) { return k >= 0 && k < SNN_MOTORES ? SNN_NOME_MOTOR[k] : ""; }
API const char *zs_nome_jeito(int k) { return k >= 0 && k < SNN_EMOCOES ? SNN_NOME_JEITO[k] : "EQUILIBRADO"; }
API const char *zs_humor_nome(void) { return face_mood_name(); }

// memoria (para a janela guardar o que o Zeus simulado aprendeu)
API int zs_salvar(uint8_t *buf, int max) { return snn_salvar(buf, max); }
API int zs_carregar(const uint8_t *buf, int n) { return snn_carregar(buf, n); }
