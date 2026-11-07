// Simulador do display do Zeus no PC: roda o MESMO zeus_face.c do firmware,
// com um framebuffer em memoria, e grava cada cena como imagem PBM em sim/out/.
// Gerar o PNG de conferencia: py -3.13 sim\render_sheet.py
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "oled_port.h"
#include "zeus_face.h"

static unsigned char fb[OLED_H][OLED_W];
static uint32_t rng = 12345;

void oled_clear(void) { memset(fb, 0, sizeof(fb)); }
void oled_pixel(int x, int y, int on)
{
    if (x >= 0 && x < OLED_W && y >= 0 && y < OLED_H) fb[y][x] = on ? 1 : 0;
}
void oled_flush(void) {}
uint32_t oled_rand(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void dump(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pbm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P1\n%d %d\n", OLED_W, OLED_H);
    for (int y = 0; y < OLED_H; y++)
    {
        for (int x = 0; x < OLED_W; x++) fputc(fb[y][x] ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}

// Avanca o relogio renderizando a 25 fps (FRAME_MS do firmware = 40 ms)
static uint32_t run(uint32_t now, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 40)
    {
        now += 40;
        face_tick(now);
        face_render(now);
    }
    return now;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "out";
    static const char *names[MOOD_COUNT] = {
        "neutro", "feliz", "triste", "bravo", "sono", "curioso", "surpreso", "pensando"
    };
    uint32_t now = 1000;
    face_init(now);

    face_splash("abra no celular:", "192.168.15.201", 3000, now);
    now = run(now, 400);
    dump(dir, "00_splash");
    now = run(now, 3000);

    for (int m = 0; m < MOOD_COUNT; m++)
    {
        face_mood_cmd(m, 0, 0, now);
        now = run(now, 1200);                  // tempo de convergir os olhos
        char n[32];
        snprintf(n, sizeof(n), "%02d_%s", m + 1, names[m]);
        dump(dir, n);
    }

    face_mood_cmd(MOOD_NEUTRO, 0, 0, now);
    now = run(now, 1200);
    // forca uma piscada: avanca ate o meio de uma (menos pixels acesos no azul)
    for (int i = 0; i < 400; i++)
    {
        now = run(now, 40);
        int lit = 0;
        for (int y = 17; y < OLED_H; y++)
            for (int x = 0; x < OLED_W; x++) lit += fb[y][x];
        if (lit < 300) break;                   // olho quase fechado no meio da piscada
    }
    dump(dir, "10_piscando");

    // formas de olhar: alguns instantes de cada humor
    static const int olhares[] = { MOOD_NEUTRO, MOOD_CURIOSO, MOOD_BRAVO, MOOD_PENSANDO };
    for (int m = 0; m < 4; m++)
    {
        face_mood_cmd(olhares[m], 0, 0, now);
        for (int i = 0; i < 3; i++)
        {
            now = run(now, 900 + 400 * i);
            char n[40];
            snprintf(n, sizeof(n), "09_olhar_%s_%d", names[olhares[m]], i + 1);
            dump(dir, n);
        }
    }

    // animacao: 12 s de rosto vivo (neutro, curioso, bravo, sono) em anim/
    if (argc > 2)
    {
        static const int seq[] = { MOOD_NEUTRO, MOOD_CURIOSO, MOOD_BRAVO, MOOD_SONO };
        int fr = 0;
        for (int m = 0; m < 4; m++)
        {
            face_mood_cmd(seq[m], 0, 0, now);
            for (int t = 0; t < 4000; t += 40)
            {
                now = run(now, 40);
                char n[40];
                snprintf(n, sizeof(n), "f%04d", fr++);
                dump(argv[2], n);
            }
        }
    }

    face_mood_cmd(MOOD_FELIZ, 0, 0, now);
    for (int i = 0; i < 4; i++) { face_talk(1, 0, now); now = run(now, 160); }
    dump(dir, "11_falando");

    // boca seguindo as palavras: pulso, logo depois (aberta) e 300 ms depois (fechando)
    face_mood_cmd(MOOD_NEUTRO, 0, 0, now);
    face_talk(1, 1, now);
    now = run(now, 80);
    dump(dir, "11b_boca_aberta");
    now = run(now, 240);
    dump(dir, "11c_boca_fechando");
    face_talk(1, 1, now);
    now = run(now, 40);
    dump(dir, "11d_boca_abrindo");
    face_talk(0, 0, now);
    now = run(now, 600);
    dump(dir, "11e_parou_de_falar");

    face_show_text("Bitcoin: R$ 612.300, subiu 2,1% hoje. Ótimo!", MOOD_FELIZ, now);
    now = run(now, 1500);
    dump(dir, "12_texto");

    face_show_text("19:10", MOOD_NEUTRO, now);
    now = run(now, 1500);
    dump(dir, "12b_hora");
    face_show_text("21 graus", MOOD_CURIOSO, now);
    now = run(now, 1500);
    dump(dir, "12c_curto");
    face_show_text("BTC R$ 612.300", MOOD_FELIZ, now);
    now = run(now, 1500);
    dump(dir, "12d_medio");

    face_set_board("rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR",
                   0, "", "VOCE", "brancas", "", now);
    now = run(now, 80);
    dump(dir, "13_xadrez_inicio");

    // Depois de 1.e4 e5 2.Nf3 Nc6 3.Bc4 Bc5 4.O-O Nf6
    face_set_board("r.bqk..rpppp.ppp..n..n....b.p.....B.P........N..PPPP.PPPRNBQ.RK.",
                   0, "g8f6", "Cf3 Cc6", "ZEUS:", "Cf6", now);
    now = run(now, 80);
    dump(dir, "14_xadrez_meio");

    face_set_board("r.bqk..rpppp.ppp..n..n....b.p.....B.P........N..PPPP.PPPRNBQ.RK.",
                   1, "g8f6", "XEQUE!", "", "", now);
    now = run(now, 80);
    dump(dir, "15_xadrez_pretas");

    face_board_off();
    face_think(1, "Galaxy S21 Ultra", now);     // vez do Zeus: pensando, sem tabuleiro
    now = run(now, 700);
    dump(dir, "17_calculando");
    now = run(now, 1600);
    dump(dir, "17b_calculando_vs");
    face_think(0, "", now);
    face_show_text("Cf6", MOOD_FELIZ, now);     // o lance dele, grande
    now = run(now, 1500);
    dump(dir, "18_lance_zeus");

    face_temp(1, 41.3f, now);                   // Key_Sw1 UP
    now = run(now, 1000);
    face_temp(1, 43.8f, now);
    now = run(now, 80);
    dump(dir, "19_temperatura");
    face_temp(0, 43.8f, now);                   // Key_Sw1 DOWN: volta ao rosto
    now = run(now, 80);

    face_mood_cmd(MOOD_NEUTRO, 0, 0, now);
    now = run(now, 61000);                      // sem interacao -> dorme
    dump(dir, "16_dormindo_sozinho");

    printf("ok: cenas em %s\n", dir);
    return 0;
}
