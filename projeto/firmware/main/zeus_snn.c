#include <math.h>
#include <string.h>

#include "zeus_snn.h"

// ---------------- parametros ----------------
//
// Unidades normalizadas: repouso 0, limiar 1. Cada passo e 1 ms.
// v' = (-v + I) / TAU_M, com I = ge - gi + vies + ruido. Um disparo de entrada com peso w
// soma w em ge, que cai com TAU_E: no potencial isso vale cerca de w * TAU_E / TAU_M.

#define TAU_M       20.0f       // membrana
#define TAU_E       5.0f        // corrente excitatoria
#define TAU_I       8.0f        // corrente inibitoria
#define TAU_STDP    20.0f       // tracos pre e pos
#define TAU_ADAPT   300.0f      // limiar sobe a cada disparo e volta devagar
#define TAU_DA      300.0f      // dopamina
#define TAU_ELIG    1000.0f     // traco de elegibilidade: a recompensa pode vir ate ~1 s depois
#define RUIDO       0.05f       // ruido na membrana a cada ms
#define A_MAIS      1.0f        // STDP: pre antes do pos fortalece
#define A_MENOS     0.9f        // pos antes do pre enfraquece (quase o mesmo: acaso nao vira habito)
#define ETA         0.0015f     // taxa de aprendizado (por 10 ms)
#define W_MAX       1.6f        // teto das sinapses que aprendem
#define PISO_INATO  0.5f        // nem a bronca apaga mais que metade do que nasceu com ele

#define FREQ_BASE   3.0f        // Hz de um neuronio sensorial sem estimulo
#define FREQ_MAX    110.0f      // Hz com estimulo 1

#define PL_MAX      1700        // a rede tem ~1520 sinapses que aprendem

// vies (corrente de fundo) por populacao
#define VIES_EXC    0.62f
#define VIES_INH    0.55f
#define VIES_EMO    0.50f
#define VIES_MOT    0.45f
#define VIES_DA     0.35f

const char *const SNN_NOME_SENSOR[SNN_SENSORES] = {
    "SOM", "FALA", "CONVERSA", "NOVIDADE", "TRABALHO", "JOGO", "WEB",
    "SILENCIO", "FADIGA", "TEDIO", "SONO", "CUTUCAO", "CARINHO", "BRONCA",
};
const char *const SNN_NOME_EMOCAO[SNN_EMOCOES] = {
    "ALEGRIA", "TRISTEZA", "RAIVA", "SONO", "CURIOSIDADE", "SURPRESA", "FOCO",
};
const char *const SNN_NOME_MOTOR[SNN_MOTORES] = {
    "SUSTO", "OBSERVA", "VAGA", "COMEMORA", "ACALMA", "COCHILA",
};
const char *const SNN_NOME_JEITO[SNN_EMOCOES] = {
    "ALEGRE", "MELANCOLICO", "RANZINZA", "PREGUICOSO", "CURIOSO", "ASSUSTADICO", "PENSATIVO",
};

// quanto cada estimulo (linha) puxa cada emocao: e a "natureza" do Zeus antes de aprender
static const float INATO_EMO[SNN_SENSORES][SNN_EMOCOES] = {
    //  feliz triste bravo  sono  curio surpr  pensa
    { 0.2f, 0,    0,    0,    0.5f, 0.3f, 0    },   // SOM
    { 0.6f, 0,    0,    0,    0.1f, 0,    0    },   // FALA
    { 0.7f, 0,    0,    0,    0.3f, 0,    0    },   // CONVERSA
    { 0,    0,    0,    0,    0.6f, 0.7f, 0    },   // NOVIDADE
    { 0,    0,    0,    0,    0.1f, 0,    0.9f },   // TRABALHO
    { 0.3f, 0,    0,    0,    0.2f, 0,    0.7f },   // JOGO
    { 0,    0,    0,    0,    0.7f, 0,    0.4f },   // WEB
    { 0,    0.3f, 0,    0.3f, 0,    0,    0    },   // SILENCIO
    { 0,    0.3f, 0.2f, 0.6f, 0,    0,    0    },   // FADIGA
    { 0,    0.6f, 0.2f, 0.3f, 0,    0,    0    },   // TEDIO
    { 0,    0,    0,    1.0f, 0,    0,    0    },   // SONO
    { 0,    0,    0.3f, 0,    0.2f, 0.8f, 0    },   // CUTUCAO
    { 1.0f, 0,    0,    0,    0,    0,    0    },   // CARINHO
    { 0,    0.5f, 0.8f, 0,    0,    0,    0    },   // BRONCA
};

// estimulo -> saida motora (gesto)
static const float INATO_MOT[SNN_SENSORES][SNN_MOTORES] = {
    //  susto observa vaga comemora acalma cochila
    { 0,    0.8f, 0,    0,    0,    0    },   // SOM
    { 0,    0.6f, 0,    0.2f, 0,    0    },   // FALA
    { 0,    0.6f, 0,    0.3f, 0,    0    },   // CONVERSA
    { 0.5f, 0.5f, 0.3f, 0,    0,    0    },   // NOVIDADE
    { 0,    0.3f, 0.3f, 0,    0,    0    },   // TRABALHO
    { 0,    0.6f, 0,    0.2f, 0,    0    },   // JOGO
    { 0,    0.2f, 0.6f, 0,    0,    0    },   // WEB
    { 0,    0,    0.5f, 0,    0.4f, 0    },   // SILENCIO
    { 0,    0,    0,    0,    0.5f, 0.5f },   // FADIGA
    { 0,    0,    0.8f, 0,    0,    0.2f },   // TEDIO
    { 0,    0,    0,    0,    0.3f, 1.0f },   // SONO
    { 0.9f, 0.3f, 0,    0,    0,    0    },   // CUTUCAO
    { 0,    0,    0,    1.0f, 0.3f, 0    },   // CARINHO
    { 0.6f, 0,    0,    0,    0,    0    },   // BRONCA
};

// emocao -> gesto (fixo)
static const float EMO_MOT[SNN_EMOCOES][SNN_MOTORES] = {
    { 0,    0.2f, 0,    0.8f, 0,    0    },   // feliz: comemora
    { 0,    0,    0.2f, 0,    0.5f, 0    },   // triste: se encolhe
    { 0.2f, 0.5f, 0,    0,    0,    0    },   // bravo: encara
    { 0,    0,    0,    0,    0,    1.0f },   // sono: cochila
    { 0,    0.3f, 0.9f, 0,    0,    0    },   // curioso: olha em volta
    { 0.9f, 0,    0,    0,    0,    0    },   // surpreso: susto
    { 0,    0,    0.3f, 0,    0,    0    },   // pensando
};

// quanto cada estimulo dura depois de um pulso (ms)
static const float TAU_SENSOR[SNN_SENSORES] = {
    2000, 1000, 60000, 3000, 2000, 20000, 8000, 1000, 1000, 1000, 1000, 1500, 2500, 3000,
};

// ---------------- estado ----------------

static float s_v[SNN_N], s_ge[SNN_N], s_gi[SNN_N], s_adapt[SNN_N], s_xpre[SNN_N], s_ypos[SNN_N];
static uint8_t s_refr[SNN_N];

// sinapses por neuronio de origem (CSR); as que aprendem vem primeiro em cada faixa
static uint16_t s_off[SNN_N + 1], s_npl[SNN_N], s_pini[SNN_N];
static uint16_t s_pos[SNN_SIN_MAX];
static int16_t s_w[SNN_SIN_MAX];             // pesos em 16 bits (W_ESC por unidade): metade da RAM de float
static int s_ns, s_np;

// sinapses que aprendem: elegibilidade, sinapse k e origem; e a lista por destino
static float s_elig[PL_MAX];
static uint16_t s_pk[PL_MAX], s_ppre[PL_MAX];
static uint16_t s_rin_off[SNN_N + 1], s_rin[PL_MAX];
static float s_soma_alvo[SNN_N];            // soma das entradas que aprendem (normalizacao)
static int16_t s_w0[PL_MAX];                // peso de nascenca (piso do que ele desaprende)

static uint16_t s_spk[SNN_N];
static int s_nspk;
static uint8_t s_ring[SNN_RASTER][SNN_BYTES];
static int s_ring_i;

static float s_cont[SNN_SENSORES], s_puls[SNN_SENSORES];
static volatile float s_pend[SNN_SENSORES];         // pulsos pedidos por outra tarefa
static volatile int s_ens_pedido = -1;
static volatile uint32_t s_ens_pedido_ms;
static int s_ens_emo = -1;
static uint32_t s_ens_ate;

static float s_da;
static float s_taxa_emo[SNN_EMOCOES], s_taxa_mot[SNN_MOTORES];
static float s_longo[SNN_EMOCOES];
static uint32_t s_conta_seg, s_conta_exc;
static uint32_t s_humor_cand_t;
static int s_humor_cand;
static uint32_t s_gesto_livre;

static snn_leitura_t L;
static uint32_t s_rng = 0x2E05u;

// ---------------- utilidades ----------------

static uint32_t rnd(void)
{
    uint32_t x = s_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s_rng = x;
}

static float rndf(void) { return (rnd() >> 8) * (1.0f / 16777216.0f); }

// quase normal (soma de duas uniformes, desvio 1): o formato do ruido importa para o aprendizado
static float rndn(void) { return (rndf() + rndf() - 1.0f) * 2.449f; }

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

// peso em 16 bits: 1/8192 por passo (faixa +-4). Arredondamento estocastico: mudancas menores
// que um passo ainda acontecem na media, entao o aprendizado fino nao se perde.
#define W_ESC 8192.0f
static float W(int k) { return s_w[k] * (1.0f / W_ESC); }
static float W0(int p) { return s_w0[p] * (1.0f / W_ESC); }
static int16_t qw(float w)
{
    float x = w * W_ESC;
    float f = floorf(x);
    if (rndf() < x - f) f += 1.0f;
    return (int16_t)clampf(f, -32767.0f, 32767.0f);
}

static int sensor_de(int i) { return (i - SNN_IN0) / SNN_POR_SENSOR; }
static int pref_exc(int i) { return (i - SNN_EXC0) % SNN_SENSORES; }   // estimulo preferido do oculto

static float vies(int i)
{
    if (i < SNN_INH0) return VIES_EXC;
    if (i < SNN_EMO0) return VIES_INH;
    if (i < SNN_MOT0) return VIES_EMO + 0.05f * L.traco[(i - SNN_EMO0) / SNN_POR_EMOCAO];
    if (i < SNN_DA0) return VIES_MOT;
    return VIES_DA;
}

// ---------------- montagem da rede ----------------

static void liga(int pre, int pos, float w)
{
    if (s_ns >= SNN_SIN_MAX) return;
    s_pos[s_ns] = (uint16_t)pos;
    s_w[s_ns] = (int16_t)lroundf(clampf(w, -3.9f, 3.9f) * W_ESC);
    s_ns++;
}

// uma sinapse que aprende (tem que vir antes das fixas do mesmo neuronio de origem)
static void liga_plastica(int pre, int pos, float w)
{
    if (s_np >= PL_MAX || s_ns >= SNN_SIN_MAX) return;
    s_pk[s_np] = (uint16_t)s_ns;
    s_ppre[s_np] = (uint16_t)pre;
    s_elig[s_np] = 0;
    s_w0[s_np] = (int16_t)lroundf(clampf(w, -3.9f, 3.9f) * W_ESC);
    s_np++;
    s_npl[pre]++;
    liga(pre, pos, w);
}

static int chance(float p) { return rndf() < p; }

static void montar(void)
{
    s_ns = s_np = 0;
    memset(s_npl, 0, sizeof(s_npl));
    for (int pre = 0; pre < SNN_N; pre++)
    {
        s_off[pre] = (uint16_t)s_ns;
        s_pini[pre] = (uint16_t)s_np;
        if (pre < SNN_EXC0)
        {
            // sensorial -> ocultos que preferem este estimulo (forte) e alguns ao acaso (fraco)
            int s = sensor_de(pre);
            for (int i = SNN_EXC0; i < SNN_INH0; i++)
            {
                if (pref_exc(i) == s) liga(pre, i, 0.9f + 0.1f * rndn());
                else if (chance(6.0f / SNN_NEXC)) liga(pre, i, 0.25f * rndf());
            }
            if (s == SNN_S_CARINHO)                       // carinho excita a dopamina
                for (int d = SNN_DA0; d < SNN_N; d++) liga(pre, d, 0.6f);
            // bronca corta a alegria na hora; carinho acalma raiva e tristeza (via interneuronios)
            for (int e = SNN_EMO0; e < SNN_MOT0; e++)
            {
                int k = (e - SNN_EMO0) / SNN_POR_EMOCAO;
                if (s == SNN_S_BRONCA && k == SNN_E_FELIZ) liga(pre, e, -0.9f);
                if (s == SNN_S_CARINHO && (k == SNN_E_BRAVO || k == SNN_E_TRISTE)) liga(pre, e, -0.7f);
            }
        }
        else if (pre < SNN_INH0)
        {
            int p = pref_exc(pre);
            for (int e = SNN_EMO0; e < SNN_MOT0; e++)    // oculto -> emocao (aprende)
            {
                int k = (e - SNN_EMO0) / SNN_POR_EMOCAO;
                float a = INATO_EMO[p][k];
                if (chance(a > 0 ? 0.6f : 0.1f)) liga_plastica(pre, e, clampf(0.05f + 1.5f * a + 0.05f * rndn(), 0.02f, W_MAX));
            }
            for (int m = SNN_MOT0; m < SNN_DA0; m++)     // oculto -> gesto (aprende)
            {
                int k = (m - SNN_MOT0) / SNN_POR_MOTOR;
                float a = INATO_MOT[p][k];
                if (chance(a > 0 ? 0.6f : 0.1f)) liga_plastica(pre, m, clampf(0.05f + 1.5f * a + 0.05f * rndn(), 0.02f, W_MAX));
            }
            for (int i = SNN_EXC0; i < SNN_INH0; i++)    // recorrencia: assembleias do mesmo estimulo
            {
                if (i == pre) continue;
                if (pref_exc(i) == p ? chance(0.3f) : chance(0.06f))
                    liga(pre, i, pref_exc(i) == p ? 0.35f : 0.12f * rndf() + 0.04f);
            }
            for (int i = SNN_INH0; i < SNN_EMO0; i++)
                if (chance(0.35f)) liga(pre, i, 0.3f);
        }
        else if (pre < SNN_EMO0)
        {
            for (int i = SNN_EXC0; i < SNN_INH0; i++)
                if (chance(0.35f)) liga(pre, i, -0.6f);
            for (int i = SNN_INH0; i < SNN_EMO0; i++)
                if (i != pre && chance(0.15f)) liga(pre, i, -0.4f);
        }
        else if (pre < SNN_MOT0)
        {
            int k = (pre - SNN_EMO0) / SNN_POR_EMOCAO;
            for (int e = SNN_EMO0; e < SNN_MOT0; e++)    // emocoes competem (vence uma)
            {
                if (e == pre) continue;
                if ((e - SNN_EMO0) / SNN_POR_EMOCAO == k) liga(pre, e, 0.3f);
                else if (chance(0.5f)) liga(pre, e, -0.5f);
            }
            for (int m = SNN_MOT0; m < SNN_DA0; m++)
            {
                float a = EMO_MOT[k][(m - SNN_MOT0) / SNN_POR_MOTOR];
                if (a > 0 && chance(0.7f)) liga(pre, m, 0.5f * a);
            }
        }
        else if (pre < SNN_DA0)
        {
            int k = (pre - SNN_MOT0) / SNN_POR_MOTOR;
            for (int m = SNN_MOT0; m < SNN_DA0; m++)     // um gesto por vez
            {
                if (m == pre) continue;
                if ((m - SNN_MOT0) / SNN_POR_MOTOR == k) liga(pre, m, 0.3f);
                else if (chance(0.6f)) liga(pre, m, -0.6f);
            }
        }
        // dopamina: neuromodulador, sem sinapses diretas
    }
    s_off[SNN_N] = (uint16_t)s_ns;

    // lista das sinapses que aprendem, agrupada por destino (para o STDP no disparo do pos)
    memset(s_rin_off, 0, sizeof(s_rin_off));
    for (int p = 0; p < s_np; p++) s_rin_off[s_pos[s_pk[p]] + 1]++;
    for (int i = 0; i < SNN_N; i++) s_rin_off[i + 1] += s_rin_off[i];
    static uint16_t cheio[SNN_N];
    memset(cheio, 0, sizeof(cheio));
    for (int p = 0; p < s_np; p++)
    {
        int i = s_pos[s_pk[p]];
        s_rin[s_rin_off[i] + cheio[i]++] = (uint16_t)p;
    }
    for (int i = 0; i < SNN_N; i++)
    {
        float soma = 0;
        for (int r = s_rin_off[i]; r < s_rin_off[i + 1]; r++) soma += W(s_pk[s_rin[r]]);
        s_soma_alvo[i] = soma;
    }
}

// ---------------- API ----------------

void snn_iniciar(uint32_t semente)
{
    memset(&L, 0, sizeof(L));
    s_rng = semente ? semente : 0x2E05u;
    montar();
    for (int i = 0; i < SNN_N; i++)
    {
        s_v[i] = 0.5f * rndf();
        s_ge[i] = s_gi[i] = s_adapt[i] = s_xpre[i] = s_ypos[i] = 0;
        s_refr[i] = 0;
    }
    memset(s_cont, 0, sizeof(s_cont));
    memset(s_puls, 0, sizeof(s_puls));
    for (int s = 0; s < SNN_SENSORES; s++) s_pend[s] = 0;
    memset(s_ring, 0, sizeof(s_ring));
    memset(s_taxa_emo, 0, sizeof(s_taxa_emo));
    memset(s_taxa_mot, 0, sizeof(s_taxa_mot));
    memset(s_longo, 0, sizeof(s_longo));
    s_nspk = 0;
    s_da = 0;
    s_ens_emo = -1;
    s_ens_pedido = -1;
    s_humor_cand = 0;
    s_gesto_livre = 0;
    L.gesto = -1;
    L.jeito = -1;
    L.energia = 100;
}

void snn_sensor(int s, float nivel)
{
    if (s >= 0 && s < SNN_SENSORES) s_cont[s] = clampf(nivel, 0, 1);
}

void snn_pulso(int s, float v)
{
    if (s < 0 || s >= SNN_SENSORES) return;
    s_pend[s] += v;
    if (s == SNN_S_CARINHO) L.recompensas++;
    if (s == SNN_S_BRONCA) L.punicoes++;
}

void snn_ensinar(int emocao, uint32_t ms)
{
    if (emocao < 0 || emocao >= SNN_EMOCOES) return;
    s_ens_pedido_ms = ms ? ms : 1500;
    s_ens_pedido = emocao;
}

const snn_leitura_t *snn_ler(void) { return &L; }
int snn_n_sinapses(void) { return s_ns; }
float snn_v(int i) { return (i >= 0 && i < SNN_N) ? s_v[i] : 0; }
float snn_limiar(int i) { return (i >= 0 && i < SNN_N) ? 1.0f + s_adapt[i] : 1; }

const uint8_t *snn_raster(int d)
{
    if (d < 0) d = 0;
    if (d >= SNN_RASTER) d = SNN_RASTER - 1;
    return s_ring[(s_ring_i - d + SNN_RASTER) % SNN_RASTER];
}

void snn_sinapse(int k, uint16_t *pre, uint16_t *pos, float *w)
{
    if (k < 0 || k >= s_ns) return;
    int lo = 0, hi = SNN_N - 1;                 // origem: busca binaria nas faixas
    while (lo < hi)
    {
        int mid = (lo + hi + 1) / 2;
        if (s_off[mid] <= k) lo = mid; else hi = mid - 1;
    }
    if (pre) *pre = (uint16_t)lo;
    if (pos) *pos = s_pos[k];
    if (w) *w = W(k);
}

int snn_plastica(int k)
{
    uint16_t pre = 0;
    snn_sinapse(k, &pre, NULL, NULL);
    return k - s_off[pre] < s_npl[pre];
}

// ---------------- dinamica ----------------

static void ler_pedidos(void)
{
    for (int s = 0; s < SNN_SENSORES; s++)
    {
        float p = s_pend[s];
        if (p != 0.0f)
        {
            s_pend[s] = 0;
            s_puls[s] = clampf(s_puls[s] + p, 0, 1);
        }
    }
    int e = s_ens_pedido;
    if (e >= 0)
    {
        s_ens_pedido = -1;
        s_ens_emo = e;
        s_ens_ate = L.passo + s_ens_pedido_ms;
        if (s_da >= 0) s_da = clampf(s_da + 0.3f, -0.6f, 1);   // licao do LLM e premio, mas nunca apaga uma bronca
        L.licoes++;
    }
}

static void decidir(void)
{
    // humor: a emocao mais ativa, se for clara; senao neutro. Troca so se durar 300 ms.
    int best = -1;
    float b1 = 0, b2 = 0;
    for (int k = 0; k < SNN_EMOCOES; k++)
    {
        float e = L.emo[k];
        if (e > b1) { b2 = b1; b1 = e; best = k; }
        else if (e > b2) b2 = e;
    }
    int cand = L.humor;
    if (best >= 0 && b1 >= 0.22f && b1 > 1.25f * b2) cand = best + 1;
    else if (b1 < 0.12f || (L.humor > 0 && L.emo[L.humor - 1] < 0.15f)) cand = 0;   // o humor de antes apagou
    if (cand != L.humor && L.humor > 0 && cand > 0 && b1 < L.emo[L.humor - 1] + 0.08f) cand = L.humor;
    if (cand != s_humor_cand) { s_humor_cand = cand; s_humor_cand_t = L.passo; }
    else if (cand != L.humor && L.passo - s_humor_cand_t >= 300) L.humor = cand;

    // gesto: saida motora que dispara forte, com descanso entre um e outro
    int g = -1;
    float m1 = 0;
    for (int k = 0; k < SNN_MOTORES; k++)
        if (L.mot[k] > m1) { m1 = L.mot[k]; g = k; }
    if (g >= 0 && m1 >= 0.4f && (int32_t)(L.passo - s_gesto_livre) >= 0)
    {
        L.gesto = g;
        L.gesto_n++;
        s_gesto_livre = L.passo + (g == SNN_M_COCHILA ? 4000 : 2500);
    }
}

static void por_segundo(void)
{
    // personalidade: emocoes vividas por muito tempo viram jeito
    float media = 0;
    for (int k = 0; k < SNN_EMOCOES; k++)
    {
        s_longo[k] += (L.emo[k] - s_longo[k]) / 600.0f;
        media += s_longo[k];
    }
    media /= SNN_EMOCOES;
    float mx = 0.2f;
    L.jeito = -1;
    for (int k = 0; k < SNN_EMOCOES; k++)
    {
        L.traco[k] = clampf(L.traco[k] + 0.0002f * (s_longo[k] - media) - 0.00002f * L.traco[k], -1, 1);
        if (L.traco[k] > mx) { mx = L.traco[k]; L.jeito = k; }
    }

    // normalizacao: cada neuronio mantem a soma das entradas que aprendem (elas competem)
    float soma_w = 0;
    for (int i = SNN_EXC0; i < SNN_DA0; i++)
    {
        int a = s_rin_off[i], b = s_rin_off[i + 1];
        if (a == b) continue;
        float soma = 0;
        for (int r = a; r < b; r++) soma += W(s_pk[s_rin[r]]);
        if (soma <= 0.001f) continue;
        float f = 1.0f + 0.1f * (s_soma_alvo[i] / soma - 1.0f);
        for (int r = a; r < b; r++)
        {
            int p = s_rin[r];
            int k = s_pk[p];
            s_w[k] = qw(clampf(W(k) * f, PISO_INATO * W0(p), W_MAX));
        }
    }
    for (int p = 0; p < s_np; p++) soma_w += W(s_pk[p]);
    L.peso_medio = s_np ? soma_w / s_np : 0;
    L.disparos = s_conta_seg;
    L.taxa = s_conta_exc / (float)SNN_NEXC;
    s_conta_seg = s_conta_exc = 0;
}

static void passo(void)
{
    // exp(-1/TAU) de cada constante de tempo, ja calculados
    static const float AE = 0.818731f, AI = 0.882497f, AS = 0.951229f, AA = 0.996672f, AD = 0.996672f;

    // 1. entrega os disparos do ms anterior
    for (int n = 0; n < s_nspk; n++)
    {
        int j = s_spk[n];
        for (int k = s_off[j]; k < s_off[j + 1]; k++)
        {
            float w = W(k);
            if (w >= 0) s_ge[s_pos[k]] += w;
            else s_gi[s_pos[k]] -= w;
        }
    }

    // 2. estimulos
    float nivel[SNN_SENSORES];
    for (int s = 0; s < SNN_SENSORES; s++)
    {
        s_puls[s] *= 1.0f - 1.0f / TAU_SENSOR[s];
        nivel[s] = s_cont[s] > s_puls[s] ? s_cont[s] : s_puls[s];
        L.sens[s] = nivel[s];
    }
    int ens = (s_ens_emo >= 0 && (int32_t)(s_ens_ate - L.passo) > 0) ? s_ens_emo : -1;

    // 3. neuronios
    uint8_t *bits = s_ring[s_ring_i = (s_ring_i + 1) % SNN_RASTER];
    memset(bits, 0, SNN_BYTES);
    int pop_emo[SNN_EMOCOES] = { 0 }, pop_mot[SNN_MOTORES] = { 0 }, n_da = 0;
    s_nspk = 0;
    for (int i = 0; i < SNN_N; i++)
    {
        int disparou = 0;
        if (i < SNN_EXC0)
        {
            float hz = FREQ_BASE + FREQ_MAX * nivel[sensor_de(i)];
            disparou = rndf() < hz * 0.001f;
            s_v[i] = disparou ? 1.0f : s_v[i] * 0.8f;
        }
        else if (s_refr[i])
        {
            s_refr[i]--;
            s_v[i] = 0;
        }
        else
        {
            float I = s_ge[i] - s_gi[i] + vies(i);
            if (ens >= 0 && i >= SNN_EMO0 && i < SNN_MOT0 && (i - SNN_EMO0) / SNN_POR_EMOCAO == ens) I += 1.0f;
            s_v[i] += (-s_v[i] + I) * (1.0f / TAU_M) + RUIDO * rndn();
            if (s_v[i] < -0.5f) s_v[i] = -0.5f;
            if (s_v[i] >= 1.0f + s_adapt[i])
            {
                disparou = 1;
                s_v[i] = 0;
                s_refr[i] = i >= SNN_INH0 && i < SNN_EMO0 ? 2 : 3;
                s_adapt[i] += i < SNN_INH0 ? 0.08f : 0.04f;
            }
        }
        s_ge[i] *= AE;
        s_gi[i] *= AI;
        s_adapt[i] *= AA;
        s_xpre[i] *= AS;
        s_ypos[i] *= AS;
        if (!disparou) continue;

        s_spk[s_nspk++] = (uint16_t)i;
        bits[i >> 3] |= (uint8_t)(1 << (i & 7));
        s_conta_seg++;
        if (i >= SNN_EXC0 && i < SNN_INH0) s_conta_exc++;
        else if (i >= SNN_EMO0 && i < SNN_MOT0) pop_emo[(i - SNN_EMO0) / SNN_POR_EMOCAO]++;
        else if (i >= SNN_MOT0 && i < SNN_DA0) pop_mot[(i - SNN_MOT0) / SNN_POR_MOTOR]++;
        else if (i >= SNN_DA0) n_da++;

        // STDP: este disparo como pre (enfraquece quem disparou antes) e como pos (fortalece)
        for (int p = s_pini[i], fim = s_pini[i] + s_npl[i]; p < fim; p++)
            s_elig[p] -= A_MENOS * s_ypos[s_pos[s_pk[p]]];
        for (int r = s_rin_off[i]; r < s_rin_off[i + 1]; r++)
        {
            int p = s_rin[r];
            s_elig[p] += A_MAIS * s_xpre[s_ppre[p]];
        }
        s_xpre[i] += 1.0f;
        s_ypos[i] += 1.0f;
    }

    // 4. dopamina: neuronios de recompensa sobem, bronca derruba
    s_da = s_da * AD + 0.02f * n_da - 0.0015f * nivel[SNN_S_BRONCA];
    s_da = clampf(s_da, -0.6f, 1);
    L.dopamina = s_da;

    // 5. leituras: taxa de cada populacao (Hz), suavizada
    for (int k = 0; k < SNN_EMOCOES; k++)
    {
        s_taxa_emo[k] += (pop_emo[k] * 1000.0f / SNN_POR_EMOCAO - s_taxa_emo[k]) * (1.0f / 400.0f);
        L.emo[k] = clampf(s_taxa_emo[k] / 40.0f, 0, 1);
    }
    for (int k = 0; k < SNN_MOTORES; k++)
    {
        s_taxa_mot[k] += (pop_mot[k] * 1000.0f / SNN_POR_MOTOR - s_taxa_mot[k]) * (1.0f / 150.0f);
        L.mot[k] = clampf(s_taxa_mot[k] / 50.0f, 0, 1);
    }
    L.passo++;
}

static void aprender(void)
{
    // a cada 10 ms: dw = eta * dopamina * elegibilidade (a recompensa escolhe o que fica)
    const float decai = 0.990050f;                // exp(-10/TAU_ELIG)
    float g = fabsf(s_da) < 0.01f ? 0.0f : ETA * s_da;    // sem dopamina, nada muda (e poupa CPU)
    for (int p = 0; p < s_np; p++)
    {
        s_elig[p] *= decai;
        if (g != 0.0f)
        {
            int k = s_pk[p];
            s_w[k] = qw(clampf(W(k) + g * s_elig[p], PISO_INATO * W0(p), W_MAX));
        }
    }
}

void snn_passos(int n)
{
    ler_pedidos();
    for (int t = 0; t < n; t++)
    {
        passo();
        if (L.passo % 10 == 0) aprender();
        if (L.passo % 20 == 0) decidir();
        if (L.passo % 1000 == 0) por_segundo();
    }
    L.energia = clampf(100.0f * (1.0f - 0.5f * L.sens[SNN_S_SONO] - 0.3f * L.sens[SNN_S_FADIGA]
                                 - 0.2f * L.emo[SNN_E_SONO]), 0, 100);
}

// ---------------- memoria ----------------

#define MAGICO 0x314E535Au      // "ZSN1"

int snn_tamanho_memoria(void)
{
    return 4 + 2 + s_np + SNN_EMOCOES * 4 + 3 * 4;
}

int snn_salvar(uint8_t *buf, int max)
{
    int n = snn_tamanho_memoria();
    if (max < n) return 0;
    uint32_t m = MAGICO;
    uint16_t np = (uint16_t)s_np;
    uint8_t *o = buf;
    memcpy(o, &m, 4); o += 4;
    memcpy(o, &np, 2); o += 2;
    for (int p = 0; p < s_np; p++) *o++ = (uint8_t)lroundf(W(s_pk[p]) / W_MAX * 255.0f);
    memcpy(o, L.traco, SNN_EMOCOES * 4); o += SNN_EMOCOES * 4;
    memcpy(o, &L.recompensas, 4); o += 4;
    memcpy(o, &L.punicoes, 4); o += 4;
    memcpy(o, &L.licoes, 4); o += 4;
    return (int)(o - buf);
}

int snn_carregar(const uint8_t *buf, int n)
{
    uint32_t m;
    uint16_t np;
    if (n != snn_tamanho_memoria()) return 0;
    memcpy(&m, buf, 4);
    memcpy(&np, buf + 4, 2);
    if (m != MAGICO || np != s_np) return 0;
    const uint8_t *o = buf + 6;
    for (int p = 0; p < s_np; p++) s_w[s_pk[p]] = (int16_t)lroundf(*o++ * (W_MAX / 255.0f) * W_ESC);
    memcpy(L.traco, o, SNN_EMOCOES * 4); o += SNN_EMOCOES * 4;
    memcpy(&L.recompensas, o, 4); o += 4;
    memcpy(&L.punicoes, o, 4); o += 4;
    memcpy(&L.licoes, o, 4);
    for (int k = 0; k < SNN_EMOCOES; k++) L.traco[k] = clampf(L.traco[k], -1, 1);
    // a soma alvo segue o que ele aprendeu (senao a normalizacao desfaria o aprendizado)
    for (int i = 0; i < SNN_N; i++)
    {
        float soma = 0;
        for (int r = s_rin_off[i]; r < s_rin_off[i + 1]; r++) soma += W(s_pk[s_rin[r]]);
        s_soma_alvo[i] = soma;
    }
    return 1;
}
