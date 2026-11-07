#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zeus_cerebro.h"
#include "zeus_face.h"
#include "zeus_snn.h"

#define EXT_MS          30000   // nivel continuo vindo da ponte vale isso sem renovar
#define NOVIDADE_MS     120000  // parado mais que isso: o proximo evento e novidade
#define PASSO_MAX_MS    300     // atrasou um pouco (WiFi, flash): alcanca o relogio; mais que isso, desiste

static volatile uint32_t s_t_inter;             // ultima interacao (qualquer evento)
static volatile uint8_t s_falando, s_pensando, s_tabuleiro;
static volatile float s_temp = -999.0f;
static float s_ext[SNN_SENSORES];
static uint32_t s_ext_ate[SNN_SENSORES];
static uint32_t s_t_passo;
static uint32_t s_gravado;                      // recompensas+punicoes+licoes na ultima gravacao

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

static void interagiu(uint32_t now)
{
    if (now - s_t_inter > NOVIDADE_MS) snn_pulso(SNN_S_NOVIDADE, 0.8f);
    s_t_inter = now;
}

void cerebro_iniciar(uint32_t now)
{
    snn_iniciar(CEREBRO_SEMENTE);
    s_t_inter = now;
    s_t_passo = now;
    memset(s_ext, 0, sizeof(s_ext));
    memset(s_ext_ate, 0, sizeof(s_ext_ate));
    s_gravado = 0;
}

void cerebro_humor_pedido(int m, uint32_t hold_ms, uint32_t now)
{
    interagiu(now);
    snn_pulso(SNN_S_CONVERSA, 0.25f);
    if (m >= 1 && m <= SNN_EMOCOES) snn_ensinar(m - 1, hold_ms ? (hold_ms > 4000 ? 4000 : hold_ms) : 1500);
}

void cerebro_texto(uint32_t now)
{
    interagiu(now);
    snn_pulso(SNN_S_CONVERSA, 0.25f);
}

void cerebro_tabuleiro(uint32_t now)
{
    interagiu(now);
    snn_pulso(SNN_S_JOGO, 0.6f);
}

void cerebro_tecla(uint32_t now)
{
    interagiu(now);
    snn_pulso(SNN_S_CUTUCAO, 1.0f);
}

int cerebro_estimulo(const char *nome, float v, float c, uint32_t now)
{
    int s = -1;
    if (!nome || !nome[0]) return 0;
    if (nome[0] >= '0' && nome[0] <= '9') s = atoi(nome);
    else
        for (int k = 0; k < SNN_SENSORES; k++)
        {
            const char *a = SNN_NOME_SENSOR[k], *b = nome;
            while (*a && *b && (*b == *a || *b == *a + 32)) { a++; b++; }  // sem caixa
            if (!*a && !*b) { s = k; break; }
        }
    if (s < 0 || s >= SNN_SENSORES) return 0;
    interagiu(now);
    if (v > 0) snn_pulso(s, clampf(v, 0, 1));
    if (c >= 0)
    {
        s_ext[s] = clampf(c, 0, 1);
        s_ext_ate[s] = now + EXT_MS;
    }
    if (s == SNN_S_SOM) snn_pulso(SNN_S_CONVERSA, 0.15f);
    return 1;
}

void cerebro_temperatura(float celsius)
{
    s_temp = celsius;
}

static float ext(int s, uint32_t now)
{
    return (int32_t)(s_ext_ate[s] - now) > 0 ? s_ext[s] : 0.0f;
}

void cerebro_passo(uint32_t now)
{
    uint32_t dt = now - s_t_passo;
    if (dt == 0 || dt > 0x80000000u) return;
    if (dt > PASSO_MAX_MS) dt = PASSO_MAX_MS;   // atrasou (flash, WiFi): nao tenta alcancar
    s_t_passo = now;

    if (s_falando || s_pensando) s_t_inter = now;
    float parado = (float)(now - s_t_inter);
    snn_sensor(SNN_S_SILENCIO, 1.0f - expf(-parado / 40000.0f));
    snn_sensor(SNN_S_TEDIO, clampf((parado - 90000.0f) / 240000.0f, 0, 1) + ext(SNN_S_TEDIO, now));
    snn_sensor(SNN_S_SONO, clampf((parado - 45000.0f) / 75000.0f, 0, 1) + ext(SNN_S_SONO, now));
    float t = s_temp;
    snn_sensor(SNN_S_FADIGA, (t > -100.0f ? clampf((t - 40.0f) / 20.0f, 0, 1) : 0) + ext(SNN_S_FADIGA, now));
    snn_sensor(SNN_S_FALA, s_falando ? 1.0f : ext(SNN_S_FALA, now));
    float trab = ext(SNN_S_TRABALHO, now);
    snn_sensor(SNN_S_TRABALHO, s_pensando ? 1.0f : trab);
    snn_sensor(SNN_S_JOGO, s_tabuleiro ? 0.6f : ext(SNN_S_JOGO, now));
    static const int OUTROS[] = { SNN_S_SOM, SNN_S_CONVERSA, SNN_S_NOVIDADE, SNN_S_WEB, SNN_S_CUTUCAO,
                                  SNN_S_CARINHO, SNN_S_BRONCA };
    for (size_t i = 0; i < sizeof(OUTROS) / sizeof(OUTROS[0]); i++)
        snn_sensor(OUTROS[i], ext(OUTROS[i], now));
    snn_passos((int)dt);
}

void cerebro_rosto(uint32_t now)
{
    s_falando = face_falando(now) ? 1 : 0;
    s_pensando = face_thinking(now) ? 1 : 0;
    s_tabuleiro = face_board_active(now) ? 1 : 0;
    const snn_leitura_t *L = snn_ler();
    face_snn(L->humor, L->gesto, L->gesto_n, now);
}

void cerebro_resumo(char *out, int max)
{
    const snn_leitura_t *L = snn_ler();
    int n = snprintf(out, max, "e=");
    for (int k = 0; k < SNN_EMOCOES && n < max; k++)
        n += snprintf(out + n, max - n, k ? ".%d" : "%d", (int)lroundf(L->emo[k] * 100));
    if (n < max)
        snprintf(out + n, max - n, "&en=%d&da=%d&j=%d&h=%d", (int)lroundf(L->energia),
                 (int)lroundf(L->dopamina * 100), L->jeito, L->humor);
}

int cerebro_aprendeu(void)
{
    const snn_leitura_t *L = snn_ler();
    return L->recompensas + L->punicoes + L->licoes != s_gravado;
}

void cerebro_gravado(void)
{
    const snn_leitura_t *L = snn_ler();
    s_gravado = L->recompensas + L->punicoes + L->licoes;
}
