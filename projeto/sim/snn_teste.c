// Banco de testes da SNN do Zeus no PC: cada cena aplica estimulos e confere a emocao,
// o gesto, a dopamina e o aprendizado. Sai com codigo 1 se alguma cena falhar.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "zeus_snn.h"

static const char *HUMOR[] = { "neutro", "feliz", "triste", "bravo", "sono", "curioso", "surpreso", "pensando" };
static int s_falhas;

static void zera(void)
{
    for (int s = 0; s < SNN_SENSORES; s++) snn_sensor(s, 0);
}

// roda ms com o sensor s no nivel v (s < 0: nada); devolve a media das emocoes no fim
static void roda(int ms, int s, float v, float emo[SNN_EMOCOES], int *humor, int gestos[SNN_MOTORES])
{
    if (s >= 0) snn_sensor(s, v);
    memset(emo, 0, sizeof(float) * SNN_EMOCOES);
    if (gestos) memset(gestos, 0, sizeof(int) * SNN_MOTORES);
    uint32_t g0 = snn_ler()->gesto_n;
    int amostras = 0;
    for (int t = 0; t < ms; t += 10)
    {
        snn_passos(10);
        const snn_leitura_t *L = snn_ler();
        if (t >= ms / 2)
        {
            for (int k = 0; k < SNN_EMOCOES; k++) emo[k] += L->emo[k];
            amostras++;
        }
        if (gestos && L->gesto_n != g0) { gestos[L->gesto]++; g0 = L->gesto_n; }
    }
    for (int k = 0; k < SNN_EMOCOES; k++) emo[k] /= amostras ? amostras : 1;
    *humor = snn_ler()->humor;
    if (s >= 0) snn_sensor(s, 0);
}

static void mostra(const char *nome, const float *emo, int humor, const int *gestos)
{
    const snn_leitura_t *L = snn_ler();
    printf("%-22s humor=%-9s", nome, HUMOR[humor]);
    for (int k = 0; k < SNN_EMOCOES; k++) printf(" %.4s=%3.0f", SNN_NOME_EMOCAO[k], emo[k] * 100);
    printf(" | oculto %.1f Hz, %u disp/s, DA %+.2f", L->taxa, (unsigned)L->disparos, L->dopamina);
    if (gestos)
    {
        printf(" | gestos:");
        for (int k = 0; k < SNN_MOTORES; k++) if (gestos[k]) printf(" %s x%d", SNN_NOME_MOTOR[k], gestos[k]);
    }
    printf("\n");
}

static void confere(const char *cena, int ok)
{
    printf("   %s %s\n", ok ? "OK  " : "FALHOU", cena);
    if (!ok) s_falhas++;
}

static void calma(void)
{
    float emo[SNN_EMOCOES];
    int h;
    zera();
    roda(4000, -1, 0, emo, &h, NULL);
}

static int cena(const char *nome, int s, float v, int ms, int humor_esperado, int gesto_esperado)
{
    float emo[SNN_EMOCOES];
    int h, g[SNN_MOTORES];
    snn_iniciar(0x2E05);                    // reacao de nascenca: rede nova a cada cena
    calma();
    roda(ms, s, v, emo, &h, g);
    mostra(nome, emo, h, g);
    int ok = h == humor_esperado && (gesto_esperado < 0 || g[gesto_esperado] > 0);
    char t[96];
    snprintf(t, sizeof(t), "%s -> %s%s%s", nome, HUMOR[humor_esperado],
             gesto_esperado >= 0 ? " + " : "", gesto_esperado >= 0 ? SNN_NOME_MOTOR[gesto_esperado] : "");
    confere(t, ok);
    return ok;
}

int main(void)
{
    snn_iniciar(0x2E05);
    printf("rede: %d neuronios, %d sinapses (%d aprendem), memoria %d bytes\n\n",
           SNN_N, snn_n_sinapses(), (int)(snn_tamanho_memoria() - 6 - 7 * 4 - 12), snn_tamanho_memoria());

    float emo[SNN_EMOCOES];
    int h, g[SNN_MOTORES];
    roda(5000, -1, 0, emo, &h, g);
    mostra("repouso", emo, h, g);
    confere("repouso -> neutro", h == 0);
    const snn_leitura_t *L = snn_ler();
    confere("repouso: oculto entre 0,5 e 15 Hz", L->taxa > 0.5f && L->taxa < 15.0f);

    cena("carinho", SNN_S_CARINHO, 1.0f, 3000, 1, SNN_M_COMEMORA);
    cena("bronca", SNN_S_BRONCA, 1.0f, 3000, 3, SNN_M_SUSTO);
    cena("trabalho (LLM)", SNN_S_TRABALHO, 1.0f, 4000, 7, -1);
    cena("sono", SNN_S_SONO, 1.0f, 5000, 4, SNN_M_COCHILA);
    cena("cutucao", SNN_S_CUTUCAO, 1.0f, 2000, 6, SNN_M_SUSTO);
    cena("web", SNN_S_WEB, 1.0f, 4000, 5, SNN_M_VAGA);
    cena("conversa", SNN_S_CONVERSA, 1.0f, 4000, 1, SNN_M_OBSERVA);
    cena("tedio", SNN_S_TEDIO, 1.0f, 5000, 2, -1);

    // feliz (conversa + carinho) e logo depois bronca: tem que virar bravo em ate 2 s
    snn_iniciar(0x2E05);
    calma();
    snn_pulso(SNN_S_CONVERSA, 0.6f);
    snn_pulso(SNN_S_CARINHO, 1.0f);
    snn_passos(4000);
    snn_pulso(SNN_S_BRONCA, 1.0f);
    roda(2000, -1, 0, emo, &h, g);
    mostra("alegre e leva bronca", emo, h, g);
    confere("alegre + bronca -> bravo em 2 s", h == 3);

    // professor: o LLM pede "surpreso" sem estimulo nenhum -> a rede obedece
    snn_iniciar(0x2E05);
    calma();
    snn_ensinar(SNN_E_SURPRESO, 2000);
    roda(1500, -1, 0, emo, &h, g);
    mostra("LLM pede surpreso", emo, h, g);
    confere("professor (LLM) -> surpreso", h == 6);

    // aprendizado: web (curiosidade) com carinho junto, varias vezes. Depois, web sozinha
    // tem que puxar mais alegria que antes.
    float antes[SNN_EMOCOES], depois[SNN_EMOCOES];
    calma();
    roda(3000, SNN_S_WEB, 0.7f, antes, &h, NULL);
    mostra("web antes de aprender", antes, h, NULL);
    for (int i = 0; i < 12; i++)
    {
        calma();
        snn_sensor(SNN_S_WEB, 0.7f);
        snn_passos(400);
        snn_pulso(SNN_S_CARINHO, 1.0f);
        snn_passos(1600);
        snn_sensor(SNN_S_WEB, 0);
    }
    calma();
    roda(3000, SNN_S_WEB, 0.7f, depois, &h, NULL);
    mostra("web depois de aprender", depois, h, NULL);
    printf("   alegria com web: %.0f%% -> %.0f%%, peso medio %.3f, recompensas %u\n",
           antes[SNN_E_FELIZ] * 100, depois[SNN_E_FELIZ] * 100, snn_ler()->peso_medio,
           (unsigned)snn_ler()->recompensas);
    confere("aprendeu: web sozinha ficou mais alegre", depois[SNN_E_FELIZ] > antes[SNN_E_FELIZ] + 0.05f);

    // memoria: grava, reinicia, carrega -> mesma resposta
    static uint8_t mem[4096];
    int n = snn_salvar(mem, sizeof(mem));
    snn_iniciar(0x2E05);
    confere("memoria grava e carrega", n > 0 && snn_carregar(mem, n));
    calma();
    roda(3000, SNN_S_WEB, 0.7f, emo, &h, NULL);
    mostra("web depois de reiniciar", emo, h, NULL);
    confere("lembrou depois de reiniciar", emo[SNN_E_FELIZ] > antes[SNN_E_FELIZ] + 0.03f);

    // um dia (2 h simuladas) de uso misturado, com muito carinho e algumas broncas. Depois a rede
    // tem que continuar sa: repouso neutro, sem disparar demais, e as reacoes basicas no lugar.
    {
        static const int EV[] = { SNN_S_SOM, SNN_S_CONVERSA, SNN_S_TRABALHO, SNN_S_WEB, SNN_S_JOGO,
                                  SNN_S_CARINHO, SNN_S_CARINHO, SNN_S_BRONCA, SNN_S_CUTUCAO, SNN_S_NOVIDADE };
        uint32_t semente = 12345;
        float taxa_max = 0;
        for (int min = 0; min < 120; min++)
        {
            for (int k = 0; k < 6; k++)
            {
                semente = semente * 1103515245u + 12345u;
                snn_pulso(EV[(semente >> 16) % 10], 1.0f);
                snn_passos(10000);
                if (snn_ler()->taxa > taxa_max) taxa_max = snn_ler()->taxa;
            }
            snn_sensor(SNN_S_SILENCIO, (min % 30) > 20 ? 0.8f : 0);
            snn_sensor(SNN_S_SONO, (min % 60) > 50 ? 0.9f : 0);
        }
        zera();
        snn_passos(300000);                 // 5 min de silencio: os estimulos lentos (conversa) somem
        const snn_leitura_t *D = snn_ler();
        printf("\ndepois de 2 h: oculto max %.1f Hz, peso medio %.3f, recompensas %u, punicoes %u, jeito %s\n",
               taxa_max, D->peso_medio, (unsigned)D->recompensas, (unsigned)D->punicoes,
               D->jeito >= 0 ? SNN_NOME_JEITO[D->jeito] : "EQUILIBRADO");
        printf("   personalidade:");
        for (int k = 0; k < SNN_EMOCOES; k++) printf(" %.4s %+.2f", SNN_NOME_EMOCAO[k], D->traco[k]);
        printf("\n");
        confere("2 h: atividade sem explodir (< 20 Hz)", taxa_max < 20.0f);
        static const struct { int s, humor; } BASICO[] = {
            { -1, 0 }, { SNN_S_CARINHO, 1 }, { SNN_S_BRONCA, 3 }, { SNN_S_SONO, 4 }, { SNN_S_TRABALHO, 7 },
        };
        for (int i = 0; i < 5; i++)
        {
            calma();
            roda(3000, BASICO[i].s, 1.0f, emo, &h, NULL);
            char t[64];
            snprintf(t, sizeof(t), "2 h depois: %s",
                     BASICO[i].s < 0 ? "repouso" : SNN_NOME_SENSOR[BASICO[i].s]);
            mostra(t, emo, h, NULL);
            snprintf(t, sizeof(t), "2 h depois: %s -> %s",
                     BASICO[i].s < 0 ? "repouso" : SNN_NOME_SENSOR[BASICO[i].s], HUMOR[BASICO[i].humor]);
            confere(t, h == BASICO[i].humor);
        }
    }

    // velocidade
    clock_t c0 = clock();
    snn_passos(20000);
    double s = (double)(clock() - c0) / CLOCKS_PER_SEC;
    printf("\nvelocidade no PC: %.0fx tempo real (20 s simulados em %.3f s)\n", 20.0 / s, s);

    printf("\n%s (%d falha%s)\n", s_falhas ? "FALHOU" : "TUDO OK", s_falhas, s_falhas == 1 ? "" : "s");
    return s_falhas ? 1 : 0;
}
