#ifndef __ZEUS_CEREBRO_H_
#define __ZEUS_CEREBRO_H_

#include <stdint.h>

// Liga a rede neural (zeus_snn) ao resto do Zeus: o que acontece com ele (voz, conversa,
// LLM pensando, xadrez, teclas, carinho, bronca, tempo parado, temperatura) vira estimulo;
// o humor e os gestos que a rede decide vao para o rosto. O LLM continua em paralelo:
// cada humor que ele pede tambem ensina a rede.
//
// O mesmo arquivo roda no ESP32 e no simulador do PC. As funcoes de evento so escrevem
// numeros (podem vir de qualquer tarefa); cerebro_passo() roda numa tarefa so e chama o
// rosto (quem chama segura o mutex do rosto em volta de cerebro_rosto()).

#define CEREBRO_SEMENTE 0x2E05u     // mesma semente no robo e no PC = mesma rede

void cerebro_iniciar(uint32_t now);

// eventos
void cerebro_humor_pedido(int m, uint32_t hold_ms, uint32_t now);   // LLM/app pediu um humor
void cerebro_texto(uint32_t now);                                    // apareceu um texto no OLED
void cerebro_tabuleiro(uint32_t now);                                // lance de xadrez
void cerebro_tecla(uint32_t now);                                    // apertaram uma tecla
// estimulo pelo nome ("carinho", "bronca", "som"...) ou numero; v = pulso 0..1;
// c >= 0 = nivel continuo que vale por 30 s (renove), c < 0 = sem nivel continuo
int cerebro_estimulo(const char *nome, float v, float c, uint32_t now);
void cerebro_temperatura(float celsius);

// roda a rede ate now (em passos de 1 ms, no maximo 100 ms por chamada)
void cerebro_passo(uint32_t now);
// manda humor e gesto ao rosto (segure o mutex do rosto)
void cerebro_rosto(uint32_t now);

// resumo curto para a ponte: "e=13.5.2.8.11.7.8&en=87&da=12&j=0&h=1"
void cerebro_resumo(char *out, int max);
// mudou o que vale gravar na flash desde a ultima vez (aprendeu algo)?
int cerebro_aprendeu(void);
void cerebro_gravado(void);

#endif //__ZEUS_CEREBRO_H_
