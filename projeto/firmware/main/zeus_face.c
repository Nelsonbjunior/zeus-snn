#include <math.h>
#include <stdio.h>
#include <string.h>

#include "zeus_face.h"
#include "oled_port.h"
#include "screens/ssd1306_fonts_generic.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// OLED azul/amarelo: linhas 0-15 amarelas, 16-63 azuis. Os olhos ficam so no azul.
#define FACE_TOP    17
#define FACE_CY     40
#define FACE_MAXH   44

#define SLEEP_MS    60000   // sem interacao por 60 s -> dorme
#define ANIM_MS      1300   // quanto ele "atua" antes de mostrar o texto
#define TEXT_MS     10000   // quanto o texto fica na tela
#define THINK_MS    10000   // "pensando" sem resposta volta ao neutro
#define BOARD_MS    20000   // tabuleiro fica na tela depois de cada lance
#define TALK_MS      1800   // cada aviso de "falando" vale isso
#define HOLD_MAX_MS 60000

// Olho = quadrado arredondado branco com pupila preta e um ponto de brilho.
// LID: palpebra de cima reta (px); BOT: bochecha que empurra o olho por baixo (px);
// PUP: lado da pupila (px); GY: para onde a pupila tende (-1 cima, +1 baixo).
enum { W, H, R, DY, TT, TO, HR, DYR, LID, BOT, PUP, GY, NP };
static const float LOOKS[MOOD_COUNT][NP] = {
    { 38, 36, 11,  0,  0,  0, 1.00f,  0,  0,  0, 16,  0.0f },   // NEUTRO
    { 38, 34, 11, -2,  0,  0, 1.00f,  0,  0, 13, 15, -0.3f },   // FELIZ: bochecha sobe
    { 36, 32, 10,  5,  0, 13, 1.00f,  0,  3,  0, 15,  0.8f },   // TRISTE: olha pra baixo
    { 38, 32,  9,  2, 15,  0, 1.00f,  0,  4,  0, 14,  0.2f },   // BRAVO: corte por dentro
    { 38, 36, 11,  4,  0,  0, 1.00f,  0, 21,  0, 15,  1.0f },   // SONO: palpebra pesada
    { 38, 38, 11, -2,  0,  0, 0.78f,  4,  0,  0, 16, -0.1f },   // CURIOSO: um olho menor
    { 40, 42, 13, -2,  0,  0, 1.00f,  0,  0,  0, 10,  0.0f },   // SURPRESO: pupila pequena
    { 34, 32, 10, -4,  0,  0, 0.90f, -2,  5,  0, 14, -0.9f },   // PENSANDO: olha pra cima
};

// Formas de olhar: sequencias de alvos para a pupila (x, y em decimos, -10..10),
// com quanto tempo segura cada alvo e quanto a palpebra desce junto (desconfiado).
typedef struct { int8_t x, y; uint8_t lid; uint16_t ms; } olhar_t;
#define OLHAR(...) { __VA_ARGS__ }
static const olhar_t OLHAR_FRENTE[]    = { OLHAR(0, 0, 0, 1800) };
static const olhar_t OLHAR_ESPIA_E[]   = { OLHAR(-10, 0, 0, 700), OLHAR(0, 0, 0, 900) };
static const olhar_t OLHAR_ESPIA_D[]   = { OLHAR(10, 0, 0, 700), OLHAR(0, 0, 0, 900) };
static const olhar_t OLHAR_PROCURA[]   = { OLHAR(-10, 1, 0, 520), OLHAR(10, 1, 0, 520),
                                           OLHAR(-10, 1, 0, 460), OLHAR(0, 0, 0, 900) };
static const olhar_t OLHAR_CIMA_E[]    = { OLHAR(-8, -9, 0, 1300) };
static const olhar_t OLHAR_CIMA_D[]    = { OLHAR(8, -9, 0, 1300) };
static const olhar_t OLHAR_BAIXO_E[]   = { OLHAR(-8, 9, 0, 1200) };
static const olhar_t OLHAR_BAIXO_D[]   = { OLHAR(8, 9, 0, 1200) };
static const olhar_t OLHAR_REVIRA[]    = { OLHAR(-9, 2, 0, 160), OLHAR(-7, -7, 0, 160), OLHAR(0, -10, 0, 160),
                                           OLHAR(7, -7, 0, 160), OLHAR(9, 2, 0, 400), OLHAR(0, 0, 0, 800) };
static const olhar_t OLHAR_DESCONFIA[] = { OLHAR(10, 1, 9, 1500), OLHAR(-10, 1, 9, 1200), OLHAR(0, 0, 0, 600) };
static const olhar_t OLHAR_PENSA[]     = { OLHAR(-8, -10, 0, 900), OLHAR(8, -10, 0, 900), OLHAR(-3, -10, 0, 700) };

static const struct { const olhar_t *seq; uint8_t n; } OLHARES[] = {
#define SEQ(a) { a, sizeof(a) / sizeof(a[0]) }
    SEQ(OLHAR_FRENTE), SEQ(OLHAR_ESPIA_E), SEQ(OLHAR_ESPIA_D), SEQ(OLHAR_PROCURA),          // 0-3
    SEQ(OLHAR_CIMA_E), SEQ(OLHAR_CIMA_D), SEQ(OLHAR_BAIXO_E), SEQ(OLHAR_BAIXO_D),           // 4-7
    SEQ(OLHAR_REVIRA), SEQ(OLHAR_DESCONFIA), SEQ(OLHAR_PENSA),                              // 8-:
#undef SEQ
};
// Gestos da rede neural (SNN): cada saida motora tem a sua forma de olhar
static const olhar_t GESTO_SUSTO[]    = { OLHAR(0, -3, 0, 220), OLHAR(-8, 0, 0, 260), OLHAR(8, 0, 0, 260),
                                          OLHAR(0, 0, 0, 700) };
static const olhar_t GESTO_OBSERVA[]  = { OLHAR(0, 0, 2, 2400) };
static const olhar_t GESTO_COMEMORA[] = { OLHAR(0, -4, 0, 200), OLHAR(0, 0, 0, 200), OLHAR(0, -4, 0, 200),
                                          OLHAR(0, 0, 0, 700) };
static const olhar_t GESTO_ACALMA[]   = { OLHAR(0, 3, 7, 2600), OLHAR(0, 0, 0, 800) };
static const olhar_t GESTO_COCHILA[]  = { OLHAR(0, 6, 17, 1400), OLHAR(0, 2, 5, 400), OLHAR(0, 6, 19, 1700),
                                          OLHAR(0, 0, 3, 700) };
#define GESTO_SUSTO_MS   700        // olhos arregalam
#define GESTO_PULO_MS    900        // olhos pulam de alegria

// Sorteio por humor: cada caractere e um indice de OLHARES ('0' + i); repetir = mais chance
static const char *const ESCOLHAS[MOOD_COUNT] = {
    "000001122345679",      // NEUTRO: quase sempre de frente, as vezes espia em volta
    "00000112245",          // FELIZ
    "06677",                // TRISTE: olha pro chao
    "00999",                // BRAVO: desconfiado, de canto de olho
    "0",                    // SONO
    "0112233334455678",     // CURIOSO: procura em volta o tempo todo
    "0001122",              // SURPRESO
    "::::8",                // PENSANDO: olha pra cima, revira
};
static const char *const ESCOLHAS_FALANDO = "000000012";    // falando: olha pra quem escuta

static float s_gx, s_gy, s_lid_extra;       // olhar atual (-1..1) e palpebra de desconfiado
static const olhar_t *s_seq = OLHAR_FRENTE;
static uint8_t s_seq_n = 1, s_seq_i;
static uint32_t s_t_olhar;                  // quando passa para o proximo alvo
static uint8_t s_olhar_novo;                // mudou o humor: sorteia ja um olhar do novo humor
static const char *MOOD_NAME[MOOD_COUNT] = {
    "neutro", "feliz", "triste", "bravo", "sono", "curioso", "surpreso", "pensando"
};

static float s_cur[NP], s_tgt[NP];

// rede neural: quando falou por ultimo, ultimo gesto visto, efeitos do gesto
#define SNN_VIVA_MS     3000    // sem noticia da SNN ha mais que isso: o rosto volta a se virar sozinho
#define SNN_CEDE_MS     4000    // depois de um comando (LLM, app, xadrez) a SNN espera isso para mandar
static uint32_t s_t_snn, s_t_cmd, s_gesto_n, s_susto_ate, s_pulo_ate;
static uint8_t s_snn_viva;
static zeus_mood_t s_mood = MOOD_NEUTRO;

static uint32_t s_t_blink, s_blink_dur, s_blink_gap;
static uint32_t s_t_touch;
static uint32_t s_talk_until, s_hold_until;
static uint32_t s_t_palavra;                // ultimo aviso de palavra (pulse) vindo do app
static float s_boca;                        // 0..1: quanto da boca esta na tela

#define WORD_TRACK_MS   1500    // sem aviso de palavra por mais que isso: boca no ritmo simulado
#define MOUTH_CY        55      // centro vertical da boca
#define MOUTH_EYE_LIFT  9.0f    // quanto os olhos sobem quando a boca aparece

static char s_text[128];
static uint8_t s_text_on;
static uint32_t s_t_text;

static char s_board[64];
static uint8_t s_board_on, s_flip;
static int8_t s_last_from = -1, s_last_to = -1;
static char s_lines[3][9];      // 8 colunas de 7 px ao lado do tabuleiro
static uint32_t s_board_until;

#define CALC_MS     15000   // "calculando" sem aviso de fim some sozinho
static uint32_t s_calc_until;
static char s_calc_who[19];     // a faixa tem 18 colunas

static uint8_t s_temp_on;
static float s_temp = -999.0f, s_temp_min, s_temp_max;

static char s_splash[2][24];
static uint8_t s_splash_on;
static uint32_t s_splash_until;

// now < t, valido mesmo quando o contador de ms da a volta
static int before(uint32_t now, uint32_t t) { return (int32_t)(t - now) > 0; }

// ---------------- primitivas ----------------

static void hline(int x, int y, int w, int on)
{
    for (int i = 0; i < w; i++) oled_pixel(x + i, y, on);
}

static void fill_round_rect(float fx, float fy, float fw, float fh, float fr, int on)
{
    int x = (int)lroundf(fx), y = (int)lroundf(fy);
    int w = (int)lroundf(fw), h = (int)lroundf(fh);
    int r = (int)fr;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 0) r = 0;
    for (int j = 0; j < h; j++)
    {
        float dy = -1.0f;
        if (j < r) dy = r - j - 0.5f;
        else if (j >= h - r) dy = j - (h - r) + 0.5f;
        int inset = 0;
        if (dy >= 0.0f) inset = (int)lroundf(r - sqrtf(r * r - dy * dy));
        hline(x + inset, y + j, w - 2 * inset, on);
    }
}

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void fill_triangle(float x0, float y0, float x1, float y1, float x2, float y2, int on)
{
    int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
    int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    for (int y = miny; y <= maxy; y++)
    {
        for (int x = minx; x <= maxx; x++)
        {
            float px = x + 0.5f, py = y + 0.5f;
            float a = edge(x0, y0, x1, y1, px, py);
            float b = edge(x1, y1, x2, y2, px, py);
            float c = edge(x2, y2, x0, y0, px, py);
            if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) oled_pixel(x, y, on);
        }
    }
}

// Caractere da fonte so com os pixels do traco (sem fundo solido)
static void draw_char(int x, int y, char ch, const FontDef *f, int on)
{
    if (ch < 32 || ch > 126) return;
    for (int i = 0; i < f->FontHeight; i++)
    {
        for (int bn = 1; bn <= f->DataSize; bn++)
        {
            uint16_t word = f->data[(ch - 32) * f->FontHeight * f->DataSize + (i * f->DataSize + bn) - 1];
            for (int j = 0; j < f->FontWidth; j++)
            {
                if ((uint16_t)(word << j) & 0x8000) oled_pixel(x + j + 16 * (bn - 1), y + i, on);
            }
        }
    }
}

static void draw_str(int x, int y, const char *s, const FontDef *f, int on)
{
    for (; *s; s++, x += f->FontWidth) draw_char(x, y, *s, f, on);
}

static void draw_str_centered(int x0, int width, int y, const char *s, const FontDef *f)
{
    int w = (int)strlen(s) * f->FontWidth;
    int x = x0 + (width - w) / 2;
    draw_str(x < x0 ? x0 : x, y, s, f, 1);
}

// UTF-8 -> ASCII: a fonte so tem 32..126. Acentos do portugues viram a letra
// base; aspas curvas e travessao viram ASCII; o resto (emoji etc.) some.
static const char LATIN1_TO_ASCII[] =
    "AAAAAAAC" "EEEEIIII" "DNOOOOOx" "OUUUUY?s"
    "aaaaaaac" "eeeeiiii" "dnooooo/" "ouuuuy?y";

static void utf8_to_ascii(const char *in, char *out, size_t max)
{
    const unsigned char *s = (const unsigned char *)in;
    size_t n = 0;
    while (*s && n + 1 < max)
    {
        unsigned char c = *s;
        char o = 0;
        if (c < 0x80) { o = (c >= 32 && c <= 126) ? (char)c : ' '; s++; }
        else if (c == 0xC3 && s[1]) { if (s[1] >= 0x80 && s[1] <= 0xBF) o = LATIN1_TO_ASCII[s[1] - 0x80]; s += 2; }
        else if (c == 0xC2 && s[1]) { if (s[1] == 0xB0 || s[1] == 0xBA) o = 'o'; else if (s[1] == 0xAA) o = 'a'; s += 2; }
        else if (c == 0xE2 && s[1] == 0x80 && s[2])
        {
            if (s[2] == 0x98 || s[2] == 0x99) o = '\'';
            else if (s[2] == 0x9C || s[2] == 0x9D) o = '"';
            else if (s[2] == 0x93 || s[2] == 0x94) o = '-';
            s += 3;
        }
        else
        {
            s++;
            while ((*s & 0xC0) == 0x80) s++;   // pula a sequencia inteira
        }
        if (o) out[n++] = o;
    }
    out[n] = 0;
}

// ---------------- humor ----------------

static void set_mood(zeus_mood_t m)
{
    if (m != s_mood) s_olhar_novo = 1;
    s_mood = m;
    for (int i = 0; i < NP; i++) s_tgt[i] = LOOKS[m][i];
}

static zeus_mood_t clamp_mood(int m)
{
    return (m < 0 || m >= MOOD_COUNT) ? MOOD_NEUTRO : (zeus_mood_t)m;
}

void face_init(uint32_t now)
{
    for (int i = 0; i < NP; i++) s_cur[i] = LOOKS[MOOD_NEUTRO][i];
    set_mood(MOOD_NEUTRO);
    s_t_touch = now;
    s_t_blink = now;
    s_blink_dur = 140;
    s_blink_gap = 3000;
    s_talk_until = now;
    s_hold_until = now;
}

void face_mood_cmd(int m, int weak, uint32_t hold_ms, uint32_t now)
{
    if (weak && before(now, s_hold_until)) return;
    set_mood(clamp_mood(m));
    s_t_touch = now;
    s_t_cmd = now;
    s_text_on = 0;
    if (hold_ms) s_hold_until = now + (hold_ms > HOLD_MAX_MS ? HOLD_MAX_MS : hold_ms);
}

void face_show_text(const char *utf8, int m, uint32_t now)
{
    utf8_to_ascii(utf8, s_text, sizeof(s_text));
    set_mood(clamp_mood(m));
    s_text_on = s_text[0] != 0;
    s_t_text = now;
    s_t_touch = now;
    s_t_cmd = now;
}

void face_talk(int on, int pulse, uint32_t now)
{
    s_talk_until = on ? now + TALK_MS : now;
    if (on && pulse) s_t_palavra = now;
    if (!on) s_t_palavra = now - WORD_TRACK_MS;     // a proxima fala comeca sem pulso velho
    s_t_touch = now;
    if (on) s_t_cmd = now;
}

void face_speaking(uint32_t now)
{
    if (s_mood == MOOD_PENSANDO) set_mood(MOOD_FELIZ);
    s_t_touch = now;
    s_t_cmd = now;
}

static int8_t square_index(const char *sq)
{
    if (!sq || sq[0] < 'a' || sq[0] > 'h' || sq[1] < '1' || sq[1] > '8') return -1;
    return (int8_t)(('8' - sq[1]) * 8 + (sq[0] - 'a'));   // ordem FEN: a8 = 0
}

void face_set_board(const char *board64, int flip, const char *last,
                    const char *l1, const char *l2, const char *l3, uint32_t now)
{
    for (int i = 0; i < 64; i++)
    {
        char c = board64 ? board64[i] : 0;
        if (!c) { for (; i < 64; i++) s_board[i] = '.'; break; }
        s_board[i] = strchr("PNBRQKpnbrqk", c) ? c : '.';
    }
    s_flip = flip ? 1 : 0;
    s_last_from = -1;
    s_last_to = -1;
    if (last && strlen(last) >= 4)
    {
        s_last_from = square_index(last);
        s_last_to = square_index(last + 2);
    }
    const char *src[3] = { l1, l2, l3 };
    for (int i = 0; i < 3; i++) utf8_to_ascii(src[i] ? src[i] : "", s_lines[i], sizeof(s_lines[i]));
    s_board_on = 1;
    s_board_until = now + BOARD_MS;
    s_t_touch = now;
    s_t_cmd = now;
}

void face_board_off(void)
{
    s_board_on = 0;
}

int face_board_active(uint32_t now)
{
    return s_board_on && before(now, s_board_until);
}

void face_splash(const char *l1, const char *l2, uint32_t ms, uint32_t now)
{
    utf8_to_ascii(l1, s_splash[0], sizeof(s_splash[0]));
    utf8_to_ascii(l2, s_splash[1], sizeof(s_splash[1]));
    s_splash_on = 1;
    s_splash_until = now + ms;
}

void face_think(int on, const char *who, uint32_t now)
{
    s_t_touch = now;
    s_t_cmd = now;
    if (!on)
    {
        if (before(now, s_calc_until) && s_mood == MOOD_PENSANDO) set_mood(MOOD_NEUTRO);
        s_calc_until = now;
        return;
    }
    s_calc_until = now + CALC_MS;
    utf8_to_ascii(who ? who : "", s_calc_who, sizeof(s_calc_who));
    s_text_on = 0;                                   // sem texto nem tabuleiro: so ele pensando
    s_board_on = 0;
    set_mood(MOOD_PENSANDO);
}

void face_temp(int on, float celsius, uint32_t now)
{
    if (on && !s_temp_on) { s_temp_min = 999.0f; s_temp_max = -999.0f; }   // min/max desde que abriu
    s_temp_on = on ? 1 : 0;
    s_temp = celsius;
    if (on && celsius > -100.0f)
    {
        if (celsius < s_temp_min) s_temp_min = celsius;
        if (celsius > s_temp_max) s_temp_max = celsius;
    }
    s_t_touch = now;
}

int face_temp_on(void)
{
    return s_temp_on;
}

int face_thinking(uint32_t now)
{
    return before(now, s_calc_until);
}

const char *face_mood_name(void)
{
    return MOOD_NAME[s_mood];
}

int face_falando(uint32_t now)
{
    return before(now, s_talk_until);
}

// O rosto esta livre para a rede neural? (sem texto, tabuleiro, fala, xadrez nem comando recente)
static int livre_para_snn(uint32_t now)
{
    return !s_text_on && !face_board_active(now) && !face_thinking(now) && !before(now, s_talk_until) &&
           !before(now, s_hold_until) && now - s_t_cmd > SNN_CEDE_MS && !s_temp_on;
}

static void gesto(int g, uint32_t now)
{
    static const olhar_t *const VAGA[] = { OLHAR_PROCURA, OLHAR_CIMA_E, OLHAR_CIMA_D, OLHAR_BAIXO_E,
                                           OLHAR_BAIXO_D, OLHAR_ESPIA_E, OLHAR_ESPIA_D };
    static const uint8_t VAGA_N[] = { 4, 1, 1, 1, 1, 2, 2 };
    const olhar_t *seq;
    uint8_t n;
    switch (g)
    {
    case 0: seq = GESTO_SUSTO; n = 4; s_susto_ate = now + GESTO_SUSTO_MS; break;
    case 1: seq = GESTO_OBSERVA; n = 1; break;
    case 2: { int k = (int)(oled_rand() % 7); seq = VAGA[k]; n = VAGA_N[k]; break; }
    case 3: seq = GESTO_COMEMORA; n = 4; s_pulo_ate = now + GESTO_PULO_MS; break;
    case 4: seq = GESTO_ACALMA; n = 2; break;
    case 5: seq = GESTO_COCHILA; n = 4; break;
    default: return;
    }
    s_seq = seq;
    s_seq_n = n;
    s_seq_i = 0;
    s_olhar_novo = 0;
    s_t_olhar = now + seq[0].ms;
}

void face_snn(int humor, int g, uint32_t gesto_n, uint32_t now)
{
    s_t_snn = now;
    s_snn_viva = 1;
    int livre = livre_para_snn(now);
    if (livre && clamp_mood(humor) != s_mood)
    {
        set_mood(clamp_mood(humor));
        s_t_touch = now;
    }
    if (gesto_n != s_gesto_n)
    {
        s_gesto_n = gesto_n;
        // gestos tambem enquanto um humor pedido pelo LLM esta seguro; so nao por cima de fala/texto/xadrez
        if (!s_text_on && !face_board_active(now) && !face_thinking(now) && !before(now, s_talk_until) &&
            !s_temp_on && (g != 5 || s_mood == MOOD_SONO))
            gesto(g, now);
    }
}

void face_tick(uint32_t now)
{
    if (s_snn_viva && now - s_t_snn > SNN_VIVA_MS) s_snn_viva = 0;
    if (s_text_on && now - s_t_text > ANIM_MS + TEXT_MS) { s_text_on = 0; set_mood(MOOD_NEUTRO); }
    if (face_thinking(now)) { s_t_touch = now; return; }      // calculando: nao cansa nem dorme
    if (s_snn_viva) return;                                    // a rede neural decide o humor e o sono
    if (s_mood == MOOD_PENSANDO && !s_text_on && now - s_t_touch > THINK_MS) set_mood(MOOD_NEUTRO);
    if (s_mood != MOOD_SONO && !s_text_on && !before(now, s_talk_until) && now - s_t_touch > SLEEP_MS)
        set_mood(MOOD_SONO);
}

// ---------------- olhos ----------------

// Escolhe a proxima forma de olhar quando a atual termina
static void olhar_tick(uint32_t now, int falando)
{
    if (!s_olhar_novo && before(now, s_t_olhar)) return;
    if (s_olhar_novo || ++s_seq_i >= s_seq_n)
    {
        const char *op = falando ? ESCOLHAS_FALANDO : ESCOLHAS[s_mood];
        int k = op[oled_rand() % strlen(op)] - '0';
        s_seq = OLHARES[k].seq;
        s_seq_n = OLHARES[k].n;
        s_seq_i = 0;
        s_olhar_novo = 0;
    }
    const olhar_t *o = &s_seq[s_seq_i];
    float nx = o->x / 10.0f, ny = o->y / 10.0f;
    // olhar parado de frente: segura um tempo variavel, para nao parecer relogio
    uint32_t ms = o->ms + ((o->x == 0 && o->y == 0) ? oled_rand() % 1600 : oled_rand() % 250);
    if (s_mood == MOOD_SONO) ms *= 2;
    // pulo grande de olhar: as vezes pisca junto, como gente
    if (fabsf(nx - s_gx) + fabsf(ny - s_gy) > 1.2f && oled_rand() % 3 == 0)
        s_t_blink = now - s_blink_dur - s_blink_gap - 1;
    s_t_olhar = now + ms;
}

// Olho estilo "deskbuddy": quadrado arredondado branco, pupila preta com brilho, e as
// palpebras (cortes em preto) desenhadas por cima de tudo.
static void draw_eye(float cx, float h, float dy, int left_eye, float gx, float gy, float lid)
{
    float w = s_cur[W];
    h = fminf(h, (float)FACE_MAXH);
    float r = fminf(s_cur[R], fminf(w, h) / 2);
    float x = cx - w / 2, y = FACE_CY + dy - h / 2;
    if (y < FACE_TOP) y = FACE_TOP;                  // nunca invade a faixa amarela
    if (y + h > OLED_H) y = OLED_H - h;
    fill_round_rect(x, y, w, fmaxf(h, 2.0f), r, 1);

    // pupila: anda dentro do olho, sem encostar na borda; o brilho fica no canto de cima
    float p = fminf(s_cur[PUP], h - 6.0f);
    if (p >= 5.0f)
    {
        float px = cx + gx * ((w - p) / 2 - 3.0f);
        float py = y + h / 2 + gy * ((h - p) / 2 - 2.0f);
        fill_round_rect(px - p / 2, py - p / 2, p, p, p * 0.32f, 0);
        if (p >= 9.0f)
        {
            int g = p >= 13.0f ? 4 : 3;
            fill_round_rect(px + p / 2 - g - 2, py - p / 2 + 2, (float)g, (float)g, 1.0f, 1);
        }
    }

    float in  = left_eye ? x + w : x;
    float out = left_eye ? x : x + w;
    if (lid > 0.5f) fill_round_rect(x - 1, y - 1, w + 2, fminf(lid, h) + 1, 0, 0);
    float yl = y + fminf(lid, h);
    if (s_cur[TT] > 0.5f) fill_triangle(in, yl - 1, in, yl + s_cur[TT], out, yl - 1, 0);
    if (s_cur[TO] > 0.5f) fill_triangle(out, yl - 1, out, yl + s_cur[TO], in, yl - 1, 0);
    // bochecha (feliz): um arco escuro sobe por baixo e deixa o olho em meia-lua
    if (s_cur[BOT] > 0.5f)
    {
        float bw = w + 10;
        fill_round_rect(cx - bw / 2, y + h - s_cur[BOT], bw, h, bw / 2, 0);
    }
}

static void draw_face(uint32_t now)
{
    for (int i = 0; i < NP; i++) s_cur[i] += (s_tgt[i] - s_cur[i]) * 0.18f;

    float blink = 1.0f;
    uint32_t since = now - s_t_blink;
    if (since < s_blink_dur)
    {
        blink = 1.0f - sinf((float)since / s_blink_dur * (float)M_PI) * 0.92f;
    }
    else if (since > s_blink_dur + s_blink_gap)
    {
        s_t_blink = now;
        s_blink_dur = 110 + oled_rand() % 60;
        s_blink_gap = 2500 + oled_rand() % 3500;
    }

    // falando: os olhos "respiram" junto com a fala, a cabeca balanca de leve e aparece a
    // boca. Duas senoides fora de fase imitam o ritmo irregular das silabas; quando o app
    // avisa cada palavra (face_talk com pulse), a boca abre no inicio de cada uma.
    int falando = before(now, s_talk_until) && s_mood != MOOD_SONO;

    // olhar: a pupila pula rapido para o alvo (sacada) e o olho todo acompanha um pouco
    olhar_tick(now, falando);
    const olhar_t *o = &s_seq[s_seq_i];
    float tgx = o->x / 10.0f;
    float tgy = fmaxf(-1.0f, fminf(1.0f, o->y / 10.0f + s_cur[GY]));
    float vel = s_mood == MOOD_SONO ? 0.12f : 0.45f;
    s_gx += (tgx - s_gx) * vel;
    s_gy += (tgy - s_gy) * vel;
    s_lid_extra += (o->lid - s_lid_extra) * 0.25f;
    float dx = s_gx * 5.0f;
    float dy = sinf(now / 1400.0f) * 1.5f + s_cur[DY] + s_gy * 2.0f;
    // gestos da rede neural: susto arregala os olhos, comemorar faz eles pularem
    float arregala = 1.0f;
    if (before(now, s_susto_ate)) arregala = 1.0f + 0.18f * sinf((float)(s_susto_ate - now) / GESTO_SUSTO_MS * (float)M_PI);
    if (before(now, s_pulo_ate)) dy -= fabsf(sinf((float)(s_pulo_ate - now) / 150.0f)) * 4.0f;

    // palpebra: base do humor + desconfiado; com sono ela pesa e cai de vez em quando
    float lid = s_cur[LID] + s_lid_extra;
    if (s_mood == MOOD_SONO)
    {
        float c = sinf(now / 1700.0f);
        lid += c > 0.6f ? (c - 0.6f) * 30.0f : 0.0f;  // cochila: quase fecha e reabre
    }
    s_boca += ((falando ? 1.0f : 0.0f) - s_boca) * 0.25f;
    float talk = 1.0f, abre = 0.0f;
    if (falando)
    {
        float syl = fabsf(sinf(now / 75.0f)) * (0.6f + 0.4f * sinf(now / 210.0f));
        talk = 0.85f + 0.15f * syl;
        dy += syl * 1.5f;
        dx += sinf(now / 520.0f) * 2.0f;
        uint32_t e = now - s_t_palavra;
        if (e < WORD_TRACK_MS)
        {
            // abre rapido no comeco da palavra e fecha devagar
            abre = e < 60 ? e / 60.0f : fmaxf(0.0f, 1.0f - (e - 60) / 220.0f);
            abre = fmaxf(abre, 0.25f * syl);
        }
        else
        {
            abre = syl;
        }
    }

    // com a boca na tela, os olhos sobem e encolhem um pouco para caber tudo
    float sobe = MOUTH_EYE_LIFT * s_boca;
    float encolhe = 1.0f - 0.32f * s_boca;

    // perspectiva: o olho do lado para onde ele olha fica um pouco maior
    float hl = s_cur[H] * (1.0f - 0.08f * s_gx);
    float hr = s_cur[H] * s_cur[HR] * (1.0f + 0.08f * s_gx);
    float k = blink * talk * encolhe * arregala;
    oled_clear();
    draw_eye(41 + dx, hl * k, dy - sobe, 1, s_gx, s_gy, lid * k);
    draw_eye(87 + dx, hr * k, dy - sobe + s_cur[DYR], 0, s_gx, s_gy, lid * k);

    if (s_boca > 0.05f)
    {
        float w = 12.0f + 16.0f * s_boca + 6.0f * abre;
        float h = 2.0f + 11.0f * abre * s_boca;
        float x = 64.0f + dx - w / 2, y = MOUTH_CY - h / 2;
        fill_round_rect(x, y, w, h, h / 2, 1);
        if (h > 6.0f)                                  // aberta: o miolo fica escuro
            fill_round_rect(x + 3, y + 2, w - 6, h - 4, (h - 4) / 2, 0);
    }

    // xadrez, vez do Zeus: "calculando..." na faixa amarela, alternando com o adversario
    if (face_thinking(now))
    {
        char linha[20];
        if (s_calc_who[0] && (now / 1600) % 2)          // nome comprido: sem o "vs"
            snprintf(linha, sizeof(linha), strlen(s_calc_who) <= 15 ? "vs %s" : "%s", s_calc_who);
        else
        {
            int n = (int)((now / 300) % 4);
            snprintf(linha, sizeof(linha), "calculando%.*s   ", n, "...");
            linha[13] = 0;                               // largura fixa: o texto nao pula
        }
        draw_str_centered(0, OLED_W, 3, linha, &Font_7x10);
    }

    // dormindo: "z" subindo na faixa amarela
    if (s_mood == MOOD_SONO)
    {
        for (int k = 0; k < 3; k++)
        {
            uint32_t ph = (now / 90 + k * 12) % 36;
            int x = 92 + k * 10 + (int)(sinf((now / 400.0f) + k) * 2);
            int y = 12 - (int)(ph / 3);
            if (y >= 0) draw_char(x, y, 'z', &Font_7x10, 1);
        }
    }
    oled_flush();
}

// ---------------- texto ----------------

#define TEXT_COLS   18      // 128 / 7
#define TEXT_LINES  4       // cabem 4 linhas de 11 px no azul

// Caractere ampliado (vizinho mais proximo): a hora sai em 16x26 x1,5 = 24x39 px por digito
static void draw_char_scaled(int x, int y, char ch, const FontDef *f, float esc)
{
    if (ch < 32 || ch > 126) return;
    for (int i = 0; i < f->FontHeight; i++)
    {
        for (int bn = 1; bn <= f->DataSize; bn++)
        {
            uint16_t word = f->data[(ch - 32) * f->FontHeight * f->DataSize + (i * f->DataSize + bn) - 1];
            for (int j = 0; j < f->FontWidth; j++)
            {
                if (!((uint16_t)(word << j) & 0x8000)) continue;
                int c = j + 16 * (bn - 1);
                int x0 = x + (int)(c * esc), x1 = x + (int)((c + 1) * esc);
                int y0 = y + (int)(i * esc), y1 = y + (int)((i + 1) * esc);
                for (int yy = y0; yy < y1; yy++)
                    for (int xx = x0; xx < x1; xx++) oled_pixel(xx, yy, 1);
            }
        }
    }
}

static int parece_hora(const char *s)
{
    // "9:05", "19:10" ou "19h10"
    int n = (int)strlen(s);
    if (n < 4 || n > 5) return 0;
    int sep = n - 3;
    for (int i = 0; i < n; i++)
    {
        if (i == sep) { if (s[i] != ':' && s[i] != 'h') return 0; }
        else if (s[i] < '0' || s[i] > '9') return 0;
    }
    return 1;
}

// Texto curto aparece grande, centralizado na faixa azul: hora em 24x39, ate 8 letras em
// 16x26, ate 11 em 11x18. Texto maior volta para a fonte pequena, quebrado em linhas.
static int draw_text_grande(void)
{
    int n = (int)strlen(s_text);
    const FontDef *f;
    float esc = 1.0f;
    if (parece_hora(s_text)) { f = &Font_16x26; esc = 1.5f; }
    else if (n <= 8) f = &Font_16x26;
    else if (n <= 11) f = &Font_11x18;
    else return 0;
    int cw = (int)(f->FontWidth * esc), ch = (int)(f->FontHeight * esc);
    int x = (OLED_W - n * cw) / 2;
    int y = FACE_TOP + (OLED_H - FACE_TOP - ch) / 2;
    oled_clear();
    for (int i = 0; i < n; i++) draw_char_scaled(x + i * cw, y, s_text[i], f, esc);
    oled_flush();
    return 1;
}

static void draw_text(void)
{
    if (draw_text_grande()) return;
    char lines[TEXT_LINES][TEXT_COLS + 1];
    int n = 0;
    const char *s = s_text;
    while (*s && n < TEXT_LINES)
    {
        while (*s == ' ') s++;
        int len = (int)strlen(s);
        int end = len < TEXT_COLS ? len : TEXT_COLS;
        if (end < len)
        {
            int sp = end;
            while (sp > 0 && s[sp] != ' ') sp--;
            if (sp > 0) end = sp;               // quebra na palavra
        }
        memcpy(lines[n], s, end);
        lines[n][end] = 0;
        n++;
        s += end;
    }

    oled_clear();
    int y = FACE_TOP + ((OLED_H - FACE_TOP - n * 11) > 0 ? (OLED_H - FACE_TOP - n * 11) / 2 : 0);
    for (int i = 0; i < n; i++) draw_str_centered(0, OLED_W, y + i * 11, lines[i], &Font_7x10);
    oled_flush();
}

// ---------------- xadrez ----------------

// Pecas 7x7 dentro da casa de 8x8 (coluna 7 e linha 0 ficam livres).
// Brancas: cheias. Pretas: so o contorno.
static const char *const PIECE_SPRITES[6][7] = {
    { ".......", "..###..", "..###..", "...#...", "..###..", ".#####.", ".#####." },  // P
    { "...#.#.", "..#####", ".##.###", "####.##", "...###.", "..####.", ".######" },  // N
    { "...#...", "..##.#.", "..#.##.", "..###..", "...#...", ".#####.", "#######" },  // B
    { "#.#.#.#", "#######", ".#####.", ".#####.", ".#####.", "#######", "#######" },  // R
    { "#..#..#", ".#.#.#.", ".#####.", "..###..", "..###..", ".#####.", "#######" },  // Q
    { "...#...", "..###..", "...#...", ".#####.", "..###..", ".#####.", "#######" },  // K
};

static int sprite_px(int kind, int x, int y)
{
    if (x < 0 || x > 6 || y < 0 || y > 6) return 0;
    return PIECE_SPRITES[kind][y][x] == '#';
}

static void draw_piece(int x0, int y0, char p)
{
    const char *kinds = "PNBRQK";
    const char *k = strchr(kinds, p >= 'a' ? p - 32 : p);
    if (!k) return;
    int kind = (int)(k - kinds);
    int white = p < 'a';
    for (int y = 0; y < 7; y++)
    {
        for (int x = 0; x < 7; x++)
        {
            if (!sprite_px(kind, x, y)) continue;
            int edge_px = !sprite_px(kind, x - 1, y) || !sprite_px(kind, x + 1, y) ||
                          !sprite_px(kind, x, y - 1) || !sprite_px(kind, x, y + 1);
            if (white || edge_px) oled_pixel(x0 + x, y0 + 1 + y, 1);
        }
    }
}

static void draw_board(void)
{
    oled_clear();
    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            int idx = s_flip ? (7 - row) * 8 + (7 - col) : row * 8 + col;
            int x0 = col * 8, y0 = row * 8;
            char p = s_board[idx];
            if (p != '.')
            {
                draw_piece(x0, y0, p);
            }
            else if ((row + col) & 1)                 // casa escura vazia: 4 pontos
            {
                for (int y = 2; y < 8; y += 3)
                    for (int x = 2; x < 8; x += 3) oled_pixel(x0 + x, y0 + y, 1);
            }
            if (idx == s_last_to) hline(x0, y0, 7, 1);
            else if (idx == s_last_from)
                for (int x = 0; x < 7; x += 2) oled_pixel(x0 + x, y0, 1);
        }
    }
    for (int y = 0; y < OLED_H; y++) oled_pixel(64, y, 1);   // borda do tabuleiro

    draw_str_centered(66, 62, 3, "XADREZ", &Font_7x10);
    for (int i = 0; i < 3; i++) draw_str_centered(66, 62, 20 + i * 15, s_lines[i], &Font_7x10);
    oled_flush();
}

static void draw_splash(void)
{
    oled_clear();
    draw_str_centered(0, OLED_W, 4, s_splash[0], &Font_7x10);
    const FontDef *big = strlen(s_splash[1]) * 8 <= OLED_W ? &Font_8x14 : &Font_7x10;
    draw_str_centered(0, OLED_W, 34, s_splash[1], big);
    oled_flush();
}

// Temperatura do chip: titulo na faixa amarela, valor grande no azul, minima e maxima embaixo
static void draw_temp(void)
{
    char v[12], mm[24];
    oled_clear();
    draw_str_centered(0, OLED_W, 3, "TEMP. ESP32", &Font_7x10);
    if (s_temp < -100.0f)
    {
        draw_str_centered(0, OLED_W, 32, "sem leitura", &Font_7x10);
        oled_flush();
        return;
    }
    snprintf(v, sizeof(v), "%.2f", s_temp);
    const FontDef *f = &Font_16x26;
    int n = (int)strlen(v);
    int w = n * f->FontWidth + 6 + 16;              // numero + bolinha do grau + "C"
    int x = (OLED_W - w) / 2, y = 20;
    draw_str(x, y, v, f, 1);
    int gx = x + n * f->FontWidth + 3;               // grau: anel de 5 px (a fonte nao tem o simbolo)
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy >= 3 && dx * dx + dy * dy <= 6) oled_pixel(gx + dx, y + 4 + dy, 1);
    draw_char(gx + 4, y, 'C', f, 1);
    snprintf(mm, sizeof(mm), "min %.1f  max %.1f", s_temp_min, s_temp_max);
    draw_str_centered(0, OLED_W, 52, mm, &Font_7x10);
    oled_flush();
}

void face_render(uint32_t now)
{
    if (s_splash_on && before(now, s_splash_until)) { draw_splash(); return; }
    s_splash_on = 0;
    if (s_temp_on) { draw_temp(); return; }
    if (face_board_active(now)) { draw_board(); return; }
    if (s_text_on && now - s_t_text > ANIM_MS && s_text[0]) { draw_text(); return; }
    draw_face(now);
}
