#ifndef __ZEUS_FACE_H_
#define __ZEUS_FACE_H_

#include <stdint.h>

// Rosto do Zeus: olhos animados (piscam, olham em volta, mudam de humor e
// "respiram" no ritmo da fala), texto curto e tabuleiro de xadrez.
// Adaptado do cozmo.ino (github.com/fabriciowth/cozmo-esp32, MIT).
//
// Nada aqui e thread-safe: main.c chama tudo segurando o mesmo mutex.
// Todo tempo chega como parametro (ms), entao o simulador do PC controla o relogio.

typedef enum
{
    MOOD_NEUTRO = 0,
    MOOD_FELIZ,
    MOOD_TRISTE,
    MOOD_BRAVO,
    MOOD_SONO,
    MOOD_CURIOSO,
    MOOD_SURPRESO,
    MOOD_PENSANDO,
    MOOD_COUNT
} zeus_mood_t;

void face_init(uint32_t now);

// m fora da faixa vira NEUTRO. weak=1: troca "fraca" (ouvindo/pensando), ignorada
// enquanto uma expressao pedida por voz estiver segura. hold_ms>0 segura a expressao.
void face_mood_cmd(int m, int weak, uint32_t hold_ms, uint32_t now);

// Texto curto (UTF-8, acentos viram ASCII) + humor: anima um pouco, depois mostra o texto.
void face_show_text(const char *utf8, int m, uint32_t now);

// on=1: a voz esta tocando (renova por 1.8 s); on=0: parou.
// pulse=1: comecou uma palavra agora (a boca abre); sem pulsos a boca segue um ritmo simulado.
void face_talk(int on, int pulse, uint32_t now);

// O agente comecou a responder: sai do "pensando".
void face_speaking(uint32_t now);

// Tabuleiro: 64 caracteres em ordem FEN (a8..h8, a7..h1), '.' = vazio,
// PNBRQK brancas, pnbrqk pretas. flip=1 desenha do ponto de vista das pretas.
// last = lance "e2e4" (pode ser vazio). l1..l3: linhas curtas ao lado do tabuleiro.
void face_set_board(const char *board64, int flip, const char *last,
                    const char *l1, const char *l2, const char *l3, uint32_t now);
void face_board_off(void);

// Xadrez: on=1 e a vez do Zeus. Olhos pensando e, na faixa de cima, "calculando..." alternando
// com "vs <quem>" (nome do aparelho). Renove a cada lance; on=0 (ou 15 s sem aviso) termina.
void face_think(int on, const char *who, uint32_t now);
int face_thinking(uint32_t now);

// Tela de abertura: duas linhas por alguns ms (IP do robo ao ligar).
void face_splash(const char *l1, const char *l2, uint32_t ms, uint32_t now);

// Tela da temperatura do chip (Key_Sw1 UP liga, DOWN volta ao rosto). celsius < -100 = sem leitura.
void face_temp(int on, float celsius, uint32_t now);
int face_temp_on(void);

// Rede neural (SNN): humor (0..7) e gesto (0 susto, 1 observa, 2 vaga, 3 comemora, 4 acalma,
// 5 cochila). gesto_n muda a cada gesto novo. O humor so entra com o rosto livre: comando do
// LLM/app, texto, fala, tabuleiro ou xadrez tem a vez. Chamando isto, o rosto para de dormir sozinho.
void face_snn(int humor, int gesto, uint32_t gesto_n, uint32_t now);
int face_falando(uint32_t now);

void face_tick(uint32_t now);
void face_render(uint32_t now);

const char *face_mood_name(void);
int face_board_active(uint32_t now);

#endif //__ZEUS_FACE_H_
