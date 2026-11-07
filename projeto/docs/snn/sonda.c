// Sonda da SNN do Zeus para o documento tecnico: roda a MESMA zeus_snn.c do firmware no PC
// e grava os dados dos graficos (dados\*.csv) e os numeros citados no texto (dados\valores.tex).
#include <stdio.h>
#include <string.h>

#include "zeus_snn.h"

static FILE *abre(const char *nome)
{
    char p[128];
    snprintf(p, sizeof(p), "dados/%s", nome);
    FILE *f = fopen(p, "w");
    if (!f) { printf("nao abriu %s\n", p); return stdout; }
    return f;
}

static int dispara(int i) { return (snn_raster(0)[i >> 3] >> (i & 7)) & 1; }

static void zera(void)
{
    for (int s = 0; s < SNN_SENSORES; s++) snn_sensor(s, 0);
}

static void calma(int ms) { zera(); snn_passos(ms); }

static int pop(int i)
{
    if (i < SNN_EXC0) return 0;
    if (i < SNN_INH0) return 1;
    if (i < SNN_EMO0) return 2;
    if (i < SNN_MOT0) return 3;
    if (i < SNN_DA0) return 4;
    return 5;
}

// media das emocoes na segunda metade de ms com o sensor s no nivel v
static void mede(int ms, int s, float v, float emo[SNN_EMOCOES])
{
    snn_sensor(s, v);
    memset(emo, 0, sizeof(float) * SNN_EMOCOES);
    int n = 0;
    for (int t = 0; t < ms; t += 10)
    {
        snn_passos(10);
        if (t >= ms / 2) { for (int k = 0; k < SNN_EMOCOES; k++) emo[k] += snn_ler()->emo[k]; n++; }
    }
    for (int k = 0; k < SNN_EMOCOES; k++) emo[k] /= n;
    snn_sensor(s, 0);
}

static int s_grupo_f[400], s_ngf, s_grupo_c[400], s_ngc;

static float media_grupo(const int *g, int n)
{
    float soma = 0, w;
    for (int j = 0; j < n; j++) { snn_sinapse(g[j], NULL, NULL, &w); soma += w; }
    return n ? soma / n : 0;
}

static void histograma(float *h, int nb)
{
    memset(h, 0, sizeof(float) * nb);
    int ns = snn_n_sinapses();
    for (int k = 0; k < ns; k++)
    {
        if (!snn_plastica(k)) continue;
        float w;
        snn_sinapse(k, NULL, NULL, &w);
        int b = (int)(w / 0.05f);
        if (b >= nb) b = nb - 1;
        if (b < 0) b = 0;
        h[b]++;
    }
}

// ---- a rede como grafo dirigido G = (V, E, w): graus, densidade, reciprocidade, SCC, distancias
static uint8_t s_adj[SNN_N][SNN_N];
static int s_ordem[SNN_N], s_no, s_vis[SNN_N];

static void dfs1(int u)
{
    s_vis[u] = 1;
    for (int v = 0; v < SNN_N; v++) if (s_adj[u][v] && !s_vis[v]) dfs1(v);
    s_ordem[s_no++] = u;
}

static int dfs2(int u, int c)
{
    s_vis[u] = c;
    int n = 1;
    for (int v = 0; v < SNN_N; v++) if (s_adj[v][u] && !s_vis[v]) n += dfs2(v, c);
    return n;
}

// distancia (em sinapses) de s ate cada no, por busca em largura
static void bfs(int s, int *d)
{
    static int fila[SNN_N];
    int ini = 0, fim = 0;
    for (int i = 0; i < SNN_N; i++) d[i] = -1;
    d[s] = 0;
    fila[fim++] = s;
    while (ini < fim)
    {
        int u = fila[ini++];
        for (int v = 0; v < SNN_N; v++) if (s_adj[u][v] && d[v] < 0) { d[v] = d[u] + 1; fila[fim++] = v; }
    }
}

static void grafo(FILE *V)
{
    int ns = snn_n_sinapses(), kin[SNN_N] = { 0 }, kout[SNN_N] = { 0 }, nexc = 0;
    memset(s_adj, 0, sizeof(s_adj));
    for (int k = 0; k < ns; k++)
    {
        uint16_t a, b;
        float w;
        snn_sinapse(k, &a, &b, &w);
        s_adj[a][b] = 1;
        kout[a]++;
        kin[b]++;
        nexc += w > 0;
    }
    int arestas = 0, rec = 0, laco = 0, koutmax = 0, kinmax = 0, kin0 = 0;
    for (int i = 0; i < SNN_N; i++)
    {
        if (kout[i] > koutmax) koutmax = kout[i];
        if (kin[i] > kinmax) kinmax = kin[i];
        kin0 += kin[i] == 0;
        for (int j = 0; j < SNN_N; j++)
        {
            arestas += s_adj[i][j];
            laco += i == j && s_adj[i][j];
            rec += i != j && s_adj[i][j] && s_adj[j][i];
        }
    }
    // componentes fortemente conexas (Kosaraju)
    memset(s_vis, 0, sizeof(s_vis));
    s_no = 0;
    for (int u = 0; u < SNN_N; u++) if (!s_vis[u]) dfs1(u);
    memset(s_vis, 0, sizeof(s_vis));
    int ncomp = 0, maior = 0;
    for (int k = SNN_N - 1; k >= 0; k--)
    {
        int u = s_ordem[k];
        if (s_vis[u]) continue;
        int n = dfs2(u, ++ncomp);
        if (n > maior) maior = n;
        if (n > 1) printf("grafo: componente forte com %d nos (contem o neuronio %d)\n", n, u);
    }
    // distancia media sensorial -> emocao e sensorial -> motor; diametro da maior componente
    int d[SNN_N], somae = 0, ne = 0, somam = 0, nm = 0, diam = 0;
    for (int s = 0; s < SNN_EXC0; s++)
    {
        bfs(s, d);
        for (int i = SNN_EMO0; i < SNN_MOT0; i++) if (d[i] > 0) { somae += d[i]; ne++; }
        for (int i = SNN_MOT0; i < SNN_DA0; i++) if (d[i] > 0) { somam += d[i]; nm++; }
    }
    for (int s = SNN_EXC0; s < SNN_N; s++)
    {
        bfs(s, d);
        for (int i = SNN_EXC0; i < SNN_N; i++) if (d[i] > diam && s_vis[i] == s_vis[s]) diam = d[i];
    }
    double n = SNN_N;
    fprintf(V, "\\newcommand{\\gArestas}{%d}\n\\newcommand{\\gExc}{%d}\n\\newcommand{\\gInh}{%d}\n", arestas, nexc, ns - nexc);
    fprintf(V, "\\newcommand{\\gDens}{%.4f}\n\\newcommand{\\gGrau}{%.1f}\n", arestas / (n * (n - 1)), arestas / n);
    fprintf(V, "\\newcommand{\\gKoutMax}{%d}\n\\newcommand{\\gKinMax}{%d}\n\\newcommand{\\gKinZero}{%d}\n", koutmax, kinmax, kin0);
    fprintf(V, "\\newcommand{\\gLacos}{%d}\n\\newcommand{\\gRecip}{%.1f}\n", laco, 100.0 * rec / (arestas - laco));
    fprintf(V, "\\newcommand{\\gComp}{%d}\n\\newcommand{\\gMaiorComp}{%d}\n\\newcommand{\\gDiam}{%d}\n", ncomp, maior, diam);
    fprintf(V, "\\newcommand{\\gDistEmo}{%.2f}\n\\newcommand{\\gDistMot}{%.2f}\n", (double)somae / ne, (double)somam / nm);
}

int main(void)
{
    FILE *V = abre("valores.tex");
    snn_iniciar(0x2E05);
    int ns = snn_n_sinapses(), np = 0;

    // ---- conectividade: contagem e peso medio por populacao (origem x destino)
    int cont[6][6] = { 0 }, contp[6][6] = { 0 };
    float somaw[6][6] = { 0 };
    for (int k = 0; k < ns; k++)
    {
        uint16_t a, b;
        float w;
        snn_sinapse(k, &a, &b, &w);
        int pl = snn_plastica(k);
        np += pl;
        cont[pop(a)][pop(b)]++;
        contp[pop(a)][pop(b)] += pl;
        somaw[pop(a)][pop(b)] += w;
        // grupos do experimento de aprendizado: ocultos que preferem WEB -> ALEGRIA / CURIOSIDADE
        if (pl && pop(a) == 1 && (a - SNN_EXC0) % SNN_SENSORES == SNN_S_WEB && pop(b) == 3)
        {
            int e = (b - SNN_EMO0) / SNN_POR_EMOCAO;
            if (e == SNN_E_FELIZ) s_grupo_f[s_ngf++] = k;
            if (e == SNN_E_CURIOSO) s_grupo_c[s_ngc++] = k;
        }
    }
    FILE *C = abre("conexoes.tex");
    static const char *POP[6] = { "Sensorial", "Oculto exc.", "Inibit\\'orio", "Emo\\c{c}\\~ao", "Motor", "Dopamina" };
    for (int a = 0; a < 6; a++)
    {
        fprintf(C, "%s", POP[a]);
        for (int b = 0; b < 6; b++)
        {
            if (!cont[a][b]) fprintf(C, " & ---");
            else fprintf(C, " & %d%s {\\scriptsize(%+.2f)}", cont[a][b], contp[a][b] ? "$^{\\ast}$" : "",
                         somaw[a][b] / cont[a][b]);
        }
        fprintf(C, " \\\\\n");
    }
    fclose(C);
    fprintf(V, "\\newcommand{\\vNSin}{%d}\n\\newcommand{\\vNPl}{%d}\n\\newcommand{\\vNFixa}{%d}\n", ns, np, ns - np);
    fprintf(V, "\\newcommand{\\vMem}{%d}\n", snn_tamanho_memoria());
    fprintf(V, "\\newcommand{\\vGrupoF}{%d}\n\\newcommand{\\vGrupoC}{%d}\n", s_ngf, s_ngc);
    grafo(V);

    // ---- 1. repouso
    calma(5000);
    const snn_leitura_t *L = snn_ler();
    fprintf(V, "\\newcommand{\\vTaxaRep}{%.1f}\n\\newcommand{\\vDispRep}{%u}\n", L->taxa, (unsigned)L->disparos);

    // ---- 2. um neuronio de perto + raster: carinho de 300 a 1100 ms
    snn_iniciar(0x2E05);
    calma(3000);
    FILE *F = abre("lif.csv"), *R = abre("raster.csv");
    fprintf(F, "t,vs,ss,vh,lh,sh,ve,le,se,alegria,da\n");
    fprintf(R, "t,i\n");
    const int SEN = SNN_S_CARINHO * SNN_POR_SENSOR, OC = SNN_EXC0 + SNN_S_CARINHO, EM = SNN_EMO0;
    for (int t = 0; t < 1600; t++)
    {
        if (t == 300) snn_sensor(SNN_S_CARINHO, 1.0f);
        if (t == 1100) snn_sensor(SNN_S_CARINHO, 0);
        snn_passos(1);
        L = snn_ler();
        fprintf(F, "%d,%.4f,%d,%.4f,%.4f,%d,%.4f,%.4f,%d,%.4f,%.4f\n", t, snn_v(SEN), dispara(SEN),
                snn_v(OC), snn_limiar(OC), dispara(OC), snn_v(EM), snn_limiar(EM), dispara(EM), L->emo[0], L->dopamina);
        for (int i = 0; i < SNN_N; i++) if (dispara(i)) fprintf(R, "%d,%d\n", t, i);
    }
    fclose(F);
    fclose(R);

    // ---- 3. cena: conversa + carinho, depois bronca
    snn_iniciar(0x2E05);
    calma(4000);
    F = abre("cena.csv");
    fprintf(F, "t,ale,tri,rai,son,cur,sur,foc,da,humor,gesto\n");
    uint32_t g0 = snn_ler()->gesto_n;
    for (int t = -1000; t < 7000; t += 20)
    {
        if (t == 0) { snn_pulso(SNN_S_CONVERSA, 0.6f); snn_pulso(SNN_S_CARINHO, 1.0f); }
        if (t == 4000) snn_pulso(SNN_S_BRONCA, 1.0f);
        snn_passos(20);
        L = snn_ler();
        int g = -1;
        if (L->gesto_n != g0) { g = L->gesto; g0 = L->gesto_n; }
        fprintf(F, "%d", t);
        for (int k = 0; k < SNN_EMOCOES; k++) fprintf(F, ",%.4f", L->emo[k]);
        fprintf(F, ",%.4f,%d,%d\n", L->dopamina, L->humor, g);
    }
    fclose(F);

    // ---- 4. dopamina em regime: bronca e carinho constantes
    snn_iniciar(0x2E05);
    calma(3000);
    snn_sensor(SNN_S_BRONCA, 1.0f);
    snn_passos(4000);
    fprintf(V, "\\newcommand{\\vDaBronca}{%.2f}\n", snn_ler()->dopamina);
    snn_iniciar(0x2E05);
    calma(3000);
    snn_sensor(SNN_S_CARINHO, 1.0f);
    snn_passos(4000);
    fprintf(V, "\\newcommand{\\vDaCarinho}{%.2f}\n", snn_ler()->dopamina);

    // ---- 5. aprendizado (mesmo protocolo do snn_teste): web + carinho 12 vezes
    snn_iniciar(0x2E05);
    float hist0[32], hist1[32], antes[SNN_EMOCOES], depois[SNN_EMOCOES];
    histograma(hist0, 32);
    calma(4000);
    mede(3000, SNN_S_WEB, 0.7f, antes);
    float wf0 = media_grupo(s_grupo_f, s_ngf), wc0 = media_grupo(s_grupo_c, s_ngc), pm0 = snn_ler()->peso_medio;
    F = abre("aprendizado.csv");
    FILE *T = abre("tentativas.csv");
    fprintf(F, "t,da,wf,wc,alegria\n");
    fprintf(T, "n,wf,wc\n0,%.4f,%.4f\n", wf0, wc0);
    int tt = 0;
    for (int i = 0; i < 12; i++)
    {
        zera();
        for (int t = 0; t < 6000; t += 50, tt += 50)
        {
            if (t == 4000) snn_sensor(SNN_S_WEB, 0.7f);
            if (t == 4400) snn_pulso(SNN_S_CARINHO, 1.0f);
            snn_passos(50);
            L = snn_ler();
            fprintf(F, "%.2f,%.4f,%.5f,%.5f,%.4f\n", tt / 1000.0, L->dopamina, media_grupo(s_grupo_f, s_ngf),
                    media_grupo(s_grupo_c, s_ngc), L->emo[SNN_E_FELIZ]);
        }
        snn_sensor(SNN_S_WEB, 0);
        fprintf(T, "%d,%.4f,%.4f\n", i + 1, media_grupo(s_grupo_f, s_ngf), media_grupo(s_grupo_c, s_ngc));
    }
    fclose(F);
    fclose(T);
    calma(4000);
    mede(3000, SNN_S_WEB, 0.7f, depois);
    histograma(hist1, 32);
    fprintf(V, "\\newcommand{\\vAleAntes}{%.0f}\n\\newcommand{\\vAleDepois}{%.0f}\n", antes[0] * 100, depois[0] * 100);
    fprintf(V, "\\newcommand{\\vCurAntes}{%.0f}\n\\newcommand{\\vCurDepois}{%.0f}\n", antes[4] * 100, depois[4] * 100);
    fprintf(V, "\\newcommand{\\vWfAntes}{%.3f}\n\\newcommand{\\vWfDepois}{%.3f}\n", wf0, media_grupo(s_grupo_f, s_ngf));
    fprintf(V, "\\newcommand{\\vWcAntes}{%.3f}\n\\newcommand{\\vWcDepois}{%.3f}\n", wc0, media_grupo(s_grupo_c, s_ngc));
    fprintf(V, "\\newcommand{\\vPmAntes}{%.3f}\n\\newcommand{\\vPmDepois}{%.3f}\n", pm0, snn_ler()->peso_medio);
    F = abre("histograma.csv");
    fprintf(F, "w,nasc,depois\n");
    for (int b = 0; b < 32; b++) fprintf(F, "%.3f,%.0f,%.0f\n", b * 0.05 + 0.025, hist0[b], hist1[b]);
    fclose(F);

    // ---- 6. memoria: grava, reinicia e carrega
    static uint8_t mem[4096];
    int n = snn_salvar(mem, sizeof(mem));
    snn_iniciar(0x2E05);
    int ok = snn_carregar(mem, n);
    float lembra[SNN_EMOCOES];
    calma(4000);
    mede(3000, SNN_S_WEB, 0.7f, lembra);
    fprintf(V, "\\newcommand{\\vAleLembra}{%.0f}\n\\newcommand{\\vMemOk}{%s}\n", lembra[0] * 100, ok ? "sim" : "n\\~ao");

    fclose(V);
    printf("sonda: %d sinapses (%d aprendem), grupos web->alegria %d / web->curiosidade %d\n", ns, np, s_ngf, s_ngc);
    printf("alegria com web: %.0f%% -> %.0f%% (depois de reiniciar %.0f%%)\n", antes[0] * 100, depois[0] * 100, lembra[0] * 100);
    return 0;
}
