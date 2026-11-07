#ifndef __ZEUS_SNN_H_
#define __ZEUS_SNN_H_

#include <stdint.h>

// Rede neural de disparos (SNN) do Zeus: 266 neuronios LIF e ~7 mil sinapses.
// Cada neuronio soma sinais eletricos, vaza devagar e dispara quando passa do limiar.
// Aprende por reforco: carinho libera "dopamina", que fortalece as conexoes que
// acabaram de levar a uma emocao (STDP modulada por recompensa, com traco de elegibilidade).
//
// Roda igual no ESP32 e no simulador do PC (mesma semente = mesma rede).
// Nada aqui e thread-safe: uma tarefa so chama snn_passos(); estimulos (snn_pulso,
// snn_sensor, snn_ensinar) so escrevem numeros e podem vir de outra tarefa.

// populacoes (o neuronio i pertence a uma delas pela faixa de indices)
#define SNN_SENSORES    14
#define SNN_POR_SENSOR  4
#define SNN_EMOCOES     7
#define SNN_POR_EMOCAO  4
#define SNN_MOTORES     6
#define SNN_POR_MOTOR   3

#define SNN_IN0     0                                       // 56 sensoriais (Poisson)
#define SNN_EXC0    (SNN_SENSORES * SNN_POR_SENSOR)         // 128 excitatorios ocultos
#define SNN_NEXC    128
#define SNN_INH0    (SNN_EXC0 + SNN_NEXC)                   // 32 inibitorios
#define SNN_NINH    32
#define SNN_EMO0    (SNN_INH0 + SNN_NINH)                   // 28 de emocao
#define SNN_MOT0    (SNN_EMO0 + SNN_EMOCOES * SNN_POR_EMOCAO) // 18 motores
#define SNN_DA0     (SNN_MOT0 + SNN_MOTORES * SNN_POR_MOTOR)  // 4 de dopamina
#define SNN_NDA     4
#define SNN_N       (SNN_DA0 + SNN_NDA)                     // 266

#define SNN_SIN_MAX 7500                                    // a rede tem ~7350
#define SNN_RASTER  128                                     // ms de disparos guardados
#define SNN_BYTES   ((SNN_N + 7) / 8)

typedef enum {
    SNN_S_SOM, SNN_S_FALA, SNN_S_CONVERSA, SNN_S_NOVIDADE, SNN_S_TRABALHO, SNN_S_JOGO, SNN_S_WEB,
    SNN_S_SILENCIO, SNN_S_FADIGA, SNN_S_TEDIO, SNN_S_SONO, SNN_S_CUTUCAO, SNN_S_CARINHO, SNN_S_BRONCA,
} snn_sensor_t;

// emocao k = humor k + 1 do rosto (zeus_mood_t); nenhuma forte = NEUTRO
typedef enum {
    SNN_E_FELIZ, SNN_E_TRISTE, SNN_E_BRAVO, SNN_E_SONO, SNN_E_CURIOSO, SNN_E_SURPRESO, SNN_E_PENSANDO,
} snn_emocao_t;

typedef enum {
    SNN_M_SUSTO, SNN_M_OBSERVA, SNN_M_VAGA, SNN_M_COMEMORA, SNN_M_ACALMA, SNN_M_COCHILA,
} snn_motor_t;

typedef struct {
    uint32_t passo;                 // ms simulados
    float sens[SNN_SENSORES];       // nivel de cada estimulo 0..1
    float emo[SNN_EMOCOES];         // atividade de cada emocao 0..1
    float mot[SNN_MOTORES];         // atividade de cada saida motora 0..1
    float traco[SNN_EMOCOES];       // personalidade: tendencia de cada emocao (-1..1)
    float dopamina;                 // -1..1 (negativa = bronca)
    float energia;                  // 0..100
    float taxa;                     // disparos por segundo de um neuronio oculto (media)
    float peso_medio;               // media das sinapses que aprendem
    uint32_t disparos;              // disparos no ultimo segundo (rede toda)
    uint32_t recompensas, punicoes, licoes;
    int humor;                      // 0 neutro, 1..7 = emocao + 1
    int gesto;                      // ultimo gesto (snn_motor_t) ou -1
    uint32_t gesto_n;               // conta gestos: mudou = gesto novo
    int jeito;                      // emocao mais marcada da personalidade, -1 = equilibrado
} snn_leitura_t;

extern const char *const SNN_NOME_SENSOR[SNN_SENSORES];
extern const char *const SNN_NOME_EMOCAO[SNN_EMOCOES];
extern const char *const SNN_NOME_MOTOR[SNN_MOTORES];
extern const char *const SNN_NOME_JEITO[SNN_EMOCOES];

void snn_iniciar(uint32_t semente);

// sensor continuo (silencio, sono...): o nivel segue o valor
void snn_sensor(int s, float nivel);
// evento (voz, carinho, bronca...): soma ao nivel, que depois cai sozinho
void snn_pulso(int s, float v);
// professor: o LLM pediu esta emocao; a rede e empurrada para ela por ms e aprende o contexto
void snn_ensinar(int emocao, uint32_t ms);

void snn_passos(int n);             // avanca n ms
const snn_leitura_t *snn_ler(void);

// para os visualizadores
int snn_n_sinapses(void);
void snn_sinapse(int k, uint16_t *pre, uint16_t *pos, float *w);
int snn_plastica(int k);
float snn_v(int i);                 // potencial de membrana (repouso 0)
float snn_limiar(int i);            // limiar atual (1 + adaptacao)
// disparos de um ms ja passado: d = 0 agora, 1 o anterior... (ate SNN_RASTER-1); bits por neuronio
const uint8_t *snn_raster(int d);

// memoria: pesos que aprendem + personalidade (para gravar na flash)
int snn_salvar(uint8_t *buf, int max);
int snn_carregar(const uint8_t *buf, int n);
int snn_tamanho_memoria(void);

#endif //__ZEUS_SNN_H_
