// Simulador do Zeus com a rede neural (SNN) em acao: rosto do OLED e os 266 neuronios
// disparando, as sinapses acendendo, o potencial de um neuronio, emocoes, dopamina,
// aprendizado e personalidade.
//
// Dois modos:
//  - SIMULACAO: roda aqui mesmo o codigo do firmware (zeus_cerebro.dll: rosto + SNN + cerebro),
//    com os botoes de estimulo (carinho, bronca, conversa, LLM pensando...).
//  - ROBO AO VIVO: le o cerebro de verdade do ESP32 (/snn, /snn/pesos e /frame) e manda os
//    estimulos para ele. O OLED do robo continua mostrando so o rosto.
//
// C# 5 (compilador do .NET Framework que ja vem no Windows): sem $"", sem ?. etc.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

[StructLayout(LayoutKind.Sequential)]
internal struct Leitura
{
    public uint passo;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 14)] public float[] sens;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 7)] public float[] emo;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 6)] public float[] mot;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 7)] public float[] traco;
    public float dopamina, energia, taxa, peso_medio;
    public uint disparos, recompensas, punicoes, licoes;
    public int humor, gesto;
    public uint gesto_n;
    public int jeito;
}

internal static class Nativo
{
    const string DLL = "zeus_cerebro.dll";
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_iniciar();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_avancar(int ms);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern uint zs_agora();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_quadro(byte[] fb);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_humor(int m, int hold);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_texto(byte[] utf8, int m);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_falar(int on);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_xadrez_pensa(int on);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern int zs_estimulo(byte[] nome, float v, float c);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_tecla();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_leitura(ref Leitura l);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern int zs_tamanho_leitura();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern int zs_n();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern int zs_n_sinapses();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_sinapses(ushort[] pre, ushort[] pos, float[] w, byte[] plastica);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern void zs_potenciais(float[] v, float[] limiar);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern int zs_raster(byte[] buf, int n);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern int zs_salvar(byte[] buf, int max);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] public static extern int zs_carregar(byte[] buf, int n);
}

// O que a tela precisa da rede, venha da DLL (simulacao) ou do ESP32 (ao vivo)
internal sealed class Cerebro
{
    public const int N = 266, BYTES = 34, IN0 = 0, EXC0 = 56, INH0 = 184, EMO0 = 216, MOT0 = 244, DA0 = 262;
    public const int HIST = 4096;                     // ms de disparos guardados aqui (raster)
    public byte[] Quadro = new byte[1024];
    public Leitura L;
    public float[] V = new float[N], Limiar = new float[N];
    public int NS;
    public ushort[] Pre, Pos;
    public float[] W;
    public byte[] Plastica;
    public int VersaoPesos;                           // muda quando os pesos mudam (redesenha as linhas)
    // aprendizado visivel: quanto cada sinapse acabou de mudar (+ fortaleceu, - enfraqueceu) e quando
    public float[] Muda;
    public int[] TMuda;
    public int AprendeuEm = int.MinValue / 2, Mudaram, MudancasTotal;
    public int[][] Saida;                             // as sinapses mais fortes de cada neuronio (pulsos)
    public byte[] Hist = new byte[HIST * BYTES];      // disparos por ms (anel)
    public long UltimoPasso = -1;                     // passo absoluto da ultima linha do anel
    public int Vigiado = 128;                         // neuronio do grafico de membrana
    public float[] TracoV = new float[HIST];          // potencial do vigiado por ms (anel, mesmo indice)

    public Cerebro()
    {
        L.sens = new float[14]; L.emo = new float[7]; L.mot = new float[6]; L.traco = new float[7];
        L.gesto = -1; L.jeito = -1;
        for (int i = 0; i < N; i++) Limiar[i] = 1;
    }

    public void Topologia()
    {
        NS = Nativo.zs_n_sinapses();
        Pre = new ushort[NS]; Pos = new ushort[NS]; W = new float[NS]; Plastica = new byte[NS];
        Nativo.zs_sinapses(Pre, Pos, W, Plastica);
        Muda = new float[NS];
        TMuda = new int[NS];
        MontarSaidas();
        VersaoPesos++;
    }

    void MontarSaidas()
    {
        List<int>[] l = new List<int>[N];
        for (int i = 0; i < N; i++) l[i] = new List<int>();
        for (int k = 0; k < NS; k++) if (Math.Abs(W[k]) >= 0.12f) l[Pre[k]].Add(k);
        Saida = new int[N][];
        for (int i = 0; i < N; i++)
        {
            l[i].Sort(delegate(int a, int b) { return Math.Abs(W[b]).CompareTo(Math.Abs(W[a])); });
            if (l[i].Count > 10) l[i].RemoveRange(10, l[i].Count - 10);
            Saida[i] = l[i].ToArray();
        }
    }

    // pesos novos (da DLL ou do robo): marca as sinapses que mudaram = aprendizado acontecendo
    public void NovosPesos(float[] novos)
    {
        int agora = Environment.TickCount, n = 0;
        float maior = 0;
        for (int k = 0; k < NS && k < novos.Length; k++)
        {
            float dw = novos[k] - W[k];
            if (Math.Abs(dw) >= 0.004f)
            {
                Muda[k] = Math.Max(-1f, Math.Min(1f, dw * 60f));
                if (Math.Abs(Muda[k]) < 0.35f) Muda[k] = dw > 0 ? 0.35f : -0.35f;
                TMuda[k] = agora;
                n++;
            }
            maior = Math.Max(maior, Math.Abs(dw));
            W[k] = novos[k];
        }
        if (n > 0) { AprendeuEm = agora; Mudaram = n; MudancasTotal += n; }
        if (maior >= 0.02f) { MontarSaidas(); VersaoPesos++; }
    }

    // linhas novas de disparo: rows (do mais velho ao atual) terminando no passo 'ultimo'
    public void Acrescentar(byte[] rows, int n, long ultimo, float vAtual)
    {
        long primeiro = ultimo - n + 1;
        for (int j = 0; j < n; j++)
        {
            long p = primeiro + j;
            if (p <= UltimoPasso) continue;
            int slot = (int)(p % HIST);
            Buffer.BlockCopy(rows, j * BYTES, Hist, slot * BYTES, BYTES);
            TracoV[slot] = float.NaN;
        }
        if (ultimo > UltimoPasso)
        {
            // buraco (simulacao rapida ou rede lenta): apaga o que nao veio
            for (long p = UltimoPasso + 1; p < primeiro && p >= 0; p++)
            {
                int slot = (int)(p % HIST);
                Array.Clear(Hist, slot * BYTES, BYTES);
                TracoV[slot] = float.NaN;
            }
            UltimoPasso = ultimo;
        }
        TracoV[(int)(ultimo % HIST)] = vAtual;
    }

    public bool Disparou(long passo, int i)
    {
        if (passo < 0 || passo > UltimoPasso || UltimoPasso - passo >= HIST) return false;
        return (Hist[(int)(passo % HIST) * BYTES + (i >> 3)] & (1 << (i & 7))) != 0;
    }

    public static int Populacao(int i)                // 0 sensor, 1 exc, 2 inh, 3 emocao, 4 motor, 5 dopamina
    {
        if (i < EXC0) return 0;
        if (i < INH0) return 1;
        if (i < EMO0) return 2;
        if (i < MOT0) return 3;
        if (i < DA0) return 4;
        return 5;
    }
}

internal interface IFonte
{
    string Nome { get; }
    void Atualizar(Cerebro c, int ms);                // simulacao avanca ms; ao vivo ignora
    void Estimulo(string nome, int v, int c);         // v/c em 0..100; c < 0 = sem nivel continuo
    void Humor(int m, int hold);
    void Falar(bool on);
    void XadrezPensa(bool on);
    void Texto(string t, int m);
    string Estado { get; }
    string Aviso { get; }                             // problema serio para mostrar em cima da rede (ou null)
}

internal sealed class FonteSim : IFonte
{
    readonly byte[] rows = new byte[128 * Cerebro.BYTES];
    int tPesos;
    public string Nome { get { return "SIMULACAO"; } }
    public string Estado { get { return "rede rodando no PC (mesmo codigo do ESP32)"; } }
    public string Aviso { get { return null; } }

    public FonteSim(Cerebro c)
    {
        Nativo.zs_iniciar();
        c.Topologia();
        string mem = Memoria();
        if (File.Exists(mem))
        {
            byte[] b = File.ReadAllBytes(mem);
            if (Nativo.zs_carregar(b, b.Length) != 0) c.Topologia();
        }
    }

    public static string Memoria()
    {
        return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ZeusCerebro.mem");
    }

    public void Salvar()
    {
        byte[] b = new byte[8192];
        int n = Nativo.zs_salvar(b, b.Length);
        if (n <= 0) return;
        Array.Resize(ref b, n);
        try { File.WriteAllBytes(Memoria(), b); } catch (IOException) { }
    }

    public void Atualizar(Cerebro c, int ms)
    {
        float[] v = new float[Cerebro.N];
        while (ms > 0)
        {
            int passo = Math.Min(ms, 100);
            // de 10 em 10 ms para o grafico de membrana ter o potencial de perto
            for (int t = 0; t < passo; t += 10)
            {
                Nativo.zs_avancar(10);
                Nativo.zs_potenciais(c.V, c.Limiar);
                int n = Nativo.zs_raster(rows, 10);
                Nativo.zs_leitura(ref c.L);
                c.Acrescentar(rows, n, (long)c.L.passo - 1, c.V[c.Vigiado]);
            }
            ms -= passo;
        }
        Nativo.zs_quadro(c.Quadro);
        tPesos += 1;
        if (tPesos >= 10)
        {
            tPesos = 0;
            ushort[] a = new ushort[c.NS], b = new ushort[c.NS];
            float[] w = new float[c.NS];
            byte[] pl = new byte[c.NS];
            Nativo.zs_sinapses(a, b, w, pl);
            c.NovosPesos(w);
        }
    }

    static byte[] Z(string s) { return Encoding.UTF8.GetBytes(s + "\0"); }
    public void Estimulo(string nome, int v, int c) { Nativo.zs_estimulo(Z(nome), v / 100f, c < 0 ? -1f : c / 100f); }
    public void Humor(int m, int hold) { Nativo.zs_humor(m, hold); }
    public void Falar(bool on) { Nativo.zs_falar(on ? 1 : 0); }
    public void XadrezPensa(bool on) { Nativo.zs_xadrez_pensa(on ? 1 : 0); }
    public void Texto(string t, int m) { Nativo.zs_texto(Z(t), m); }
}

// Robo ao vivo: busca a foto da rede e o OLED pelo WiFi (pedidos assincronos, a tela nunca trava)
internal sealed class FonteRobo : IFonte
{
    readonly HttpClient http = new HttpClient();
    readonly string baseUrl;
    readonly Cerebro c;
    volatile bool vivo = true;
    string estado = "conectando...";
    DateTime ultimaFoto = DateTime.MinValue;
    bool primeiroPeso = true;
    public string Nome { get { return "ROBO AO VIVO"; } }
    public string Estado { get { return estado; } }
    string aviso = "procurando o cerebro do robo...";
    public string Aviso { get { return aviso; } }

    public FonteRobo(Cerebro c, string ip)
    {
        this.c = c;
        baseUrl = "http://" + ip.Trim();
        http.Timeout = TimeSpan.FromSeconds(3);
        Nativo.zs_iniciar();                          // mesma semente = mesma topologia do robo
        c.Topologia();
        Task.Run(new Func<Task>(Laco));
        Task.Run(new Func<Task>(LacoQuadro));
        Task.Run(new Func<Task>(LacoPesos));
    }

    public void Parar() { vivo = false; }

    async Task Laco()
    {
        while (vivo)
        {
            int espera = 60;
            try
            {
                HttpResponseMessage resp = await http.GetAsync(baseUrl + "/snn");
                if ((int)resp.StatusCode == 404)
                {
                    estado = "o robo respondeu, mas sem a rota /snn";
                    aviso = "O ROBO ESTA COM O FIRMWARE ANTIGO (SEM A REDE NEURAL)\ngrave o firmware novo para ver o cerebro dele ao vivo";
                    espera = 2000;
                }
                else
                {
                    resp.EnsureSuccessStatusCode();
                    byte[] b = await resp.Content.ReadAsByteArrayAsync();
                    lock (c) Ler(b);
                    ultimaFoto = DateTime.Now;
                    estado = "ligado em " + baseUrl;
                    aviso = null;
                }
            }
            catch (Exception e)
            {
                estado = "sem resposta de " + baseUrl + " (" + e.GetType().Name + ")";
                aviso = "SEM SINAL DO ROBO EM " + baseUrl + "\nconfira o IP e se ele esta ligado no WiFi";
                espera = 1000;
            }
            await Task.Delay(espera);
        }
    }

    async Task LacoQuadro()
    {
        while (vivo)
        {
            int espera = 40;
            try
            {
                byte[] b = await http.GetByteArrayAsync(baseUrl + "/frame");
                if (b.Length == 1024) lock (c) Buffer.BlockCopy(b, 0, c.Quadro, 0, 1024);
            }
            catch (Exception) { espera = 1000; }
            await Task.Delay(espera);
        }
    }

    async Task LacoPesos()
    {
        while (vivo)
        {
            try
            {
                byte[] b = await http.GetByteArrayAsync(baseUrl + "/snn/pesos");
                int ns = BitConverter.ToUInt16(b, 0);
                lock (c)
                {
                    if (ns == c.NS && b.Length >= 2 + ns)
                    {
                        float[] w = new float[ns];
                        for (int k = 0; k < ns; k++) w[k] = (sbyte)b[2 + k] / 127f * 2f;
                        if (primeiroPeso) { Array.Copy(w, c.W, ns); c.VersaoPesos++; primeiroPeso = false; }
                        else c.NovosPesos(w);
                    }
                    else estado = "a rede do robo e diferente da do simulador (" + ns + " x " + c.NS + " sinapses)";
                }
            }
            catch (Exception) { }
            await Task.Delay(1500);
        }
    }

    void Ler(byte[] b)
    {
        if (b.Length < 188 + Cerebro.N || BitConverter.ToUInt32(b, 0) != 0x4E4E535Au) return;
        int o = 4;
        uint passo = BitConverter.ToUInt32(b, o); o += 4;
        o += 4;                                        // n e ns
        c.L.humor = b[o]; c.L.gesto = (sbyte)b[o + 1]; c.L.jeito = (sbyte)b[o + 2]; int nr = b[o + 3]; o += 4;
        c.L.gesto_n = BitConverter.ToUInt32(b, o); o += 4;
        for (int k = 0; k < 14; k++, o += 4) c.L.sens[k] = BitConverter.ToSingle(b, o);
        for (int k = 0; k < 7; k++, o += 4) c.L.emo[k] = BitConverter.ToSingle(b, o);
        for (int k = 0; k < 6; k++, o += 4) c.L.mot[k] = BitConverter.ToSingle(b, o);
        for (int k = 0; k < 7; k++, o += 4) c.L.traco[k] = BitConverter.ToSingle(b, o);
        c.L.dopamina = BitConverter.ToSingle(b, o); o += 4;
        c.L.energia = BitConverter.ToSingle(b, o); o += 4;
        c.L.taxa = BitConverter.ToSingle(b, o); o += 4;
        c.L.peso_medio = BitConverter.ToSingle(b, o); o += 4;
        c.L.disparos = BitConverter.ToUInt32(b, o); o += 4;
        c.L.recompensas = BitConverter.ToUInt32(b, o); o += 4;
        c.L.punicoes = BitConverter.ToUInt32(b, o); o += 4;
        c.L.licoes = BitConverter.ToUInt32(b, o); o += 4;
        c.L.passo = passo;
        for (int i = 0; i < Cerebro.N; i++) c.V[i] = (sbyte)b[o + i] / 100f;
        o += Cerebro.N;
        if (b.Length >= o + nr * Cerebro.BYTES)
        {
            byte[] rows = new byte[nr * Cerebro.BYTES];
            Buffer.BlockCopy(b, o, rows, 0, rows.Length);
            c.Acrescentar(rows, nr, (long)passo - 1, c.V[c.Vigiado]);
        }
    }

    public void Atualizar(Cerebro c, int ms)
    {
        if ((DateTime.Now - ultimaFoto).TotalSeconds > 5 && ultimaFoto != DateTime.MinValue) estado = "o robo parou de responder";
    }

    void Get(string caminho)
    {
        Task.Run(async () => { try { await http.GetAsync(baseUrl + caminho); } catch (Exception) { } });
    }

    public void Estimulo(string nome, int v, int cc) { Get("/snn?s=" + nome + "&v=" + v + (cc >= 0 ? "&c=" + cc : "")); }
    public void Humor(int m, int hold) { Get("/mood?m=" + m + "&hold=" + hold); }
    public void Falar(bool on) { Get("/talk?on=" + (on ? 1 : 0)); }
    public void XadrezPensa(bool on) { Get("/think?on=" + (on ? 1 : 0) + "&n=simulador"); }
    public void Texto(string t, int m) { Get("/show?t=" + Uri.EscapeDataString(t) + "&m=" + m); }
}

// Botao desenhado na tela (mesmo estilo dos paineis)
internal sealed class Botao
{
    public RectangleF R;
    public string Texto;
    public Action Clique;
    public Func<bool> Aceso;
    public DateTime Flash;
}

internal sealed class Tela : Control
{
    // paleta do video: verde neon no preto
    static readonly Color FUNDO = Color.FromArgb(4, 7, 5), PAINEL = Color.FromArgb(8, 14, 10);
    static readonly Color VERDE = Color.FromArgb(92, 255, 122), VERDE_M = Color.FromArgb(52, 170, 80);
    static readonly Color VERDE_E = Color.FromArgb(22, 70, 34), VERDE_EE = Color.FromArgb(14, 36, 20);
    static readonly Color[] COR_POP = {
        Color.FromArgb(150, 255, 190), Color.FromArgb(92, 255, 122), Color.FromArgb(255, 176, 70),
        Color.FromArgb(200, 255, 90), Color.FromArgb(90, 225, 255), Color.FromArgb(255, 95, 200) };
    static readonly string[] NOME_POP = { "SENSORIAL", "OCULTO EXC", "OCULTO INIB", "EMOCAO", "MOTOR", "DOPAMINA" };
    static readonly string[] SENSOR = { "SOM", "FALA", "CONVERSA", "NOVIDADE", "TRABALHO", "JOGO", "WEB",
        "SILENCIO", "FADIGA", "TEDIO", "SONO", "CUTUCAO", "CARINHO", "BRONCA" };
    static readonly string[] EMOCAO = { "ALEGRIA", "TRISTEZA", "RAIVA", "SONO", "CURIOSIDADE", "SURPRESA", "FOCO" };
    static readonly string[] MOTOR = { "SUSTO", "OBSERVA", "VAGA", "COMEMORA", "ACALMA", "COCHILA" };
    static readonly string[] JEITO = { "ALEGRE", "MELANCOLICO", "RANZINZA", "PREGUICOSO", "CURIOSO", "ASSUSTADICO", "PENSATIVO" };
    static readonly string[,] HUMOR = {
        { "ATENTO", "CALMO" }, { "A VONTADE", "ALEGRE" }, { "CABISBAIXO", "TRISTE" }, { "IRRITADO", "BRAVO" },
        { "CANSADO", "SONOLENTO" }, { "CURIOSO", "EXPLORANDO" }, { "ASSUSTADO", "SURPRESO" }, { "ATENTO", "FOCADO" } };

    readonly Cerebro c;
    public IFonte Fonte;
    public int Velocidade = 1;
    public bool Pausado, Falando, LlmPensando, Xadrez;
    readonly List<Botao> botoes = new List<Botao>();
    readonly Font fTitulo, fPeq, fMed, fGrande, fEnorme, fBotao;
    readonly PointF[] posNo = new PointF[Cerebro.N];
    RectangleF rRede, rRaster, rMembrana, rOled;
    Bitmap linhas;                                    // sinapses (redesenha quando os pesos mudam)
    int versaoLinhas = -1;
    Size tamLinhas;
    uint recVisto, punVisto, licVisto;
    DateTime flashRec = DateTime.MinValue, flashPun = DateTime.MinValue, flashLic = DateTime.MinValue;
    readonly float[] histDa = new float[600], histPeso = new float[600];
    int iHist;
    DateTime tHist = DateTime.Now;
    readonly Random rnd = new Random(266);

    public Tela(Cerebro c)
    {
        this.c = c;
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        BackColor = FUNDO;
        string cond = FonteExiste("Bahnschrift") ? "Bahnschrift" : "Segoe UI";
        fTitulo = new Font("Consolas", 8.5f, FontStyle.Bold);
        fPeq = new Font("Consolas", 8f);
        fMed = new Font("Consolas", 10f, FontStyle.Bold);
        fGrande = new Font(cond, 20f, FontStyle.Bold);
        fEnorme = new Font(cond, 30f, FontStyle.Bold);
        fBotao = new Font("Consolas", 8.5f, FontStyle.Bold);
        CriarBotoes();
    }

    static bool FonteExiste(string nome)
    {
        using (Font f = new Font(nome, 10)) return f.Name == nome;
    }

    void B(string t, Action a, Func<bool> aceso)
    {
        Botao b = new Botao();
        b.Texto = t; b.Clique = a; b.Aceso = aceso;
        botoes.Add(b);
    }

    void CriarBotoes()
    {
        B("CARINHO", delegate { Fonte.Estimulo("carinho", 100, -1); }, null);
        B("BRONCA", delegate { Fonte.Estimulo("bronca", 100, -1); }, null);
        B("CUTUCAO", delegate { Fonte.Estimulo("cutucao", 100, -1); }, null);
        B("FALAR COM ELE", delegate { Fonte.Estimulo("som", 100, -1); Fonte.Estimulo("conversa", 40, -1); }, null);
        B("NOVIDADE", delegate { Fonte.Estimulo("novidade", 100, -1); }, null);
        B("PESQUISA WEB", delegate { Fonte.Estimulo("web", 100, -1); }, null);
        B("LANCE XADREZ", delegate { Fonte.Estimulo("jogo", 80, -1); }, null);
        B("LLM PENSANDO", delegate { LlmPensando = !LlmPensando; Fonte.Estimulo("trabalho", 0, LlmPensando ? 100 : 0); },
          delegate { return LlmPensando; });
        B("ZEUS FALANDO", delegate { Falando = !Falando; Fonte.Falar(Falando); }, delegate { return Falando; });
        B("XADREZ CALCULA", delegate { Xadrez = !Xadrez; Fonte.XadrezPensa(Xadrez); }, delegate { return Xadrez; });
        B("LLM: FELIZ", delegate { Fonte.Humor(1, 4000); }, null);
        B("LLM: TRISTE", delegate { Fonte.Humor(2, 4000); }, null);
        B("LLM: BRAVO", delegate { Fonte.Humor(3, 4000); }, null);
        B("LLM: SURPRESO", delegate { Fonte.Humor(6, 4000); }, null);
        B("TEXTO \"OLA!\"", delegate { Fonte.Texto("Ola!", 1); }, null);
        B("VELOC. x1", delegate { Velocidade = Velocidade == 1 ? 8 : Velocidade == 8 ? 32 : 1; }, null);
        B("PAUSAR", delegate { Pausado = !Pausado; }, delegate { return Pausado; });
        B("DEMO", delegate { demo = demo < 0 ? 0 : -1; demoPasso = 0; }, delegate { return demo >= 0; });
    }

    // DEMO: uma conversa roteirizada (segundos desde o inicio, o que acontece)
    int demo = -1, demoPasso;
    public string DemoAgora = "";
    struct Cena { public double t; public string nome; public Action a; public Cena(double t, string n, Action a) { this.t = t; nome = n; this.a = a; } }
    Cena[] Roteiro()
    {
        return new Cena[] {
            new Cena(0, "alguem chega e fala com ele", delegate { Fonte.Estimulo("som", 100, -1); Fonte.Estimulo("conversa", 60, -1); }),
            new Cena(2, "ele responde (falando)", delegate { Falando = true; Fonte.Falar(true); }),
            new Cena(5, "parou de falar", delegate { Falando = false; Fonte.Falar(false); }),
            new Cena(6, "CARINHO: recompensa (dopamina)", delegate { Fonte.Estimulo("carinho", 100, -1); }),
            new Cena(10, "pergunta dificil: LLM pensando", delegate { LlmPensando = true; Fonte.Estimulo("trabalho", 0, 100); }),
            new Cena(16, "LLM respondeu e pediu cara feliz", delegate { LlmPensando = false; Fonte.Estimulo("trabalho", 0, 0); Fonte.Humor(1, 4000); }),
            new Cena(22, "BRONCA: punicao (dopamina cai)", delegate { Fonte.Estimulo("bronca", 100, -1); }),
            new Cena(28, "CUTUCAO: tecla apertada", delegate { Fonte.Estimulo("cutucao", 100, -1); }),
            new Cena(32, "pesquisa na web", delegate { Fonte.Estimulo("web", 100, -1); }),
            new Cena(38, "xadrez: Zeus calculando", delegate { Xadrez = true; Fonte.XadrezPensa(true); }),
            new Cena(44, "lance feito", delegate { Xadrez = false; Fonte.XadrezPensa(false); Fonte.Estimulo("jogo", 80, -1); }),
            new Cena(50, "CARINHO de novo", delegate { Fonte.Estimulo("carinho", 100, -1); }),
            new Cena(56, "deixado sozinho (x8: tedio e sono)", delegate { Velocidade = 8; }),
            new Cena(76, "fim da demo", delegate { Velocidade = 1; }),
        };
    }
    DateTime demoInicio;
    public void IniciarDemo() { demo = 0; demoPasso = 0; }
    void DemoTick()
    {
        if (demo < 0) { DemoAgora = ""; return; }
        Cena[] r = Roteiro();
        if (demoPasso == 0 && demo == 0) { demoInicio = DateTime.Now; demo = 1; }
        double t = (DateTime.Now - demoInicio).TotalSeconds;
        while (demoPasso < r.Length && r[demoPasso].t <= t) { r[demoPasso].a(); DemoAgora = r[demoPasso].nome; demoPasso++; }
        if (demoPasso >= r.Length) demo = -1;
    }

    public void Tick(int ms)
    {
        DemoTick();
        if (!Pausado) lock (c) Fonte.Atualizar(c, ms * (Fonte is FonteSim ? Velocidade : 1));
        if ((DateTime.Now - tHist).TotalMilliseconds >= 100)
        {
            tHist = DateTime.Now;
            histDa[iHist] = c.L.dopamina;
            histPeso[iHist] = c.L.peso_medio;
            iHist = (iHist + 1) % histDa.Length;
        }
        if (c.L.recompensas != recVisto) { if (recVisto != 0 || c.L.recompensas == 1) flashRec = DateTime.Now; recVisto = c.L.recompensas; }
        if (c.L.punicoes != punVisto) { if (punVisto != 0 || c.L.punicoes == 1) flashPun = DateTime.Now; punVisto = c.L.punicoes; }
        if (c.L.licoes != licVisto) { if (licVisto != 0 || c.L.licoes == 1) flashLic = DateTime.Now; licVisto = c.L.licoes; }
        Invalidate();
    }

    // ---------------- desenho ----------------

    RectangleF Painel(Graphics g, float x, float y, float w, float h, string titulo, string tag)
    {
        RectangleF r = new RectangleF(x, y, w, h);
        using (SolidBrush b = new SolidBrush(PAINEL)) g.FillRectangle(b, r);
        for (int i = 3; i >= 1; i--)                    // brilho da borda
            using (Pen p = new Pen(Color.FromArgb(18 * (4 - i), VERDE), i * 2)) g.DrawRectangle(p, x, y, w, h);
        using (Pen p = new Pen(VERDE_M, 1)) g.DrawRectangle(p, x, y, w, h);
        using (SolidBrush b = new SolidBrush(VERDE_M)) g.DrawString("// " + titulo, fTitulo, b, x + 8, y + 5);
        if (tag != null)
        {
            SizeF s = g.MeasureString(tag + " \u25CF", fTitulo);
            using (SolidBrush b = new SolidBrush(VERDE_M)) g.DrawString(tag + " \u25CF", fTitulo, b, x + w - s.Width - 6, y + 5);
        }
        using (Pen p = new Pen(VERDE_EE)) g.DrawLine(p, x + 6, y + 21, x + w - 6, y + 21);
        return new RectangleF(x + 8, y + 26, w - 16, h - 32);
    }

    void Texto(Graphics g, string t, Font f, Color cor, float x, float y)
    {
        using (SolidBrush b = new SolidBrush(cor)) g.DrawString(t, f, b, x, y);
    }

    void Brilho(Graphics g, string t, Font f, Color cor, float x, float y)
    {
        using (SolidBrush b = new SolidBrush(Color.FromArgb(40, cor)))
            for (int dx = -2; dx <= 2; dx += 2) for (int dy = -2; dy <= 2; dy += 2) g.DrawString(t, f, b, x + dx, y + dy);
        Texto(g, t, f, cor, x, y);
    }

    void Barra(Graphics g, float x, float y, float w, float h, float v, Color cor)
    {
        using (SolidBrush b = new SolidBrush(VERDE_EE)) g.FillRectangle(b, x, y, w, h);
        using (SolidBrush b = new SolidBrush(cor)) g.FillRectangle(b, x, y, w * Math.Max(0, Math.Min(1, v)), h);
        using (Pen p = new Pen(VERDE_E)) g.DrawRectangle(p, x, y, w, h);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        Graphics g = e.Graphics;
        g.Clear(FUNDO);
        g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
        float W = ClientSize.Width, H = ClientSize.Height, m = 10;
        if (W < 600 || H < 400) return;
        lock (c)
        {
            // coluna esquerda: rosto, emocoes, personalidade
            float colE = Math.Max(300, W * 0.24f);
            float hOled = colE / 2 + 34;
            DesenharOled(g, m, m, colE, hOled);
            float hEmo = 268;
            DesenharEmocoes(g, m, m * 2 + hOled, colE, hEmo);
            DesenharPersonalidade(g, m, m * 3 + hOled + hEmo, colE, H - (m * 4 + hOled + hEmo));

            // centro: sistema nervoso, raster e membrana
            float colD = Math.Max(300, W * 0.24f);
            float xC = colE + m * 2, wC = W - colE - colD - m * 4;
            float hRede = (H - m * 3) * 0.6f;
            DesenharRede(g, xC, m, wC, hRede);
            float hBaixo = H - hRede - m * 3;
            DesenharRaster(g, xC, m * 2 + hRede, wC * 0.58f, hBaixo);
            DesenharMembrana(g, xC + wC * 0.58f + m, m * 2 + hRede, wC * 0.42f - m, hBaixo);

            // direita: estimulos (sentidos + botoes), dopamina
            float xD = W - colD - m;
            float hDa = 210;
            DesenharEstimulos(g, xD, m, colD, H - hDa - m * 3);
            DesenharDopamina(g, xD, H - hDa - m, colD, hDa);
        }
    }

    void DesenharOled(Graphics g, float x, float y, float w, float h)
    {
        RectangleF r = Painel(g, x, y, w, h, "ZEUS.exe", Fonte.Nome);
        float esc = Math.Min(r.Width / 128f, (r.Height - 4) / 64f);
        float ox = r.X + (r.Width - 128 * esc) / 2, oy = r.Y + 2;
        rOled = new RectangleF(ox, oy, 128 * esc, 64 * esc);
        using (SolidBrush fundo = new SolidBrush(Color.FromArgb(6, 8, 12))) g.FillRectangle(fundo, rOled);
        using (SolidBrush am = new SolidBrush(Color.FromArgb(255, 210, 63)))
        using (SolidBrush az = new SolidBrush(Color.FromArgb(79, 195, 247)))
            for (int py = 0; py < 64; py++)
                for (int px = 0; px < 128; px++)
                    if ((c.Quadro[(py >> 3) * 128 + px] & (1 << (py & 7))) != 0)
                        g.FillRectangle(py < 16 ? am : az, ox + px * esc, oy + py * esc, esc - (esc >= 4 ? 1 : 0), esc - (esc >= 4 ? 1 : 0));
    }

    void DesenharEmocoes(Graphics g, float x, float y, float w, float h)
    {
        RectangleF r = Painel(g, x, y, w, h, "EMOCOES AGORA", "ESTADO");
        int hm = Math.Max(0, Math.Min(7, c.L.humor));
        SizeF s1 = g.MeasureString(HUMOR[hm, 0], fEnorme), s2 = g.MeasureString(HUMOR[hm, 1], fGrande);
        Brilho(g, HUMOR[hm, 0], fEnorme, VERDE, r.X + (r.Width - s1.Width) / 2, r.Y - 2);
        Texto(g, HUMOR[hm, 1], fGrande, Color.FromArgb(220, 255, 230), r.X + (r.Width - s2.Width) / 2, r.Y + 42);
        string jeito = "JEITO: " + (c.L.jeito >= 0 && c.L.jeito < 7 ? JEITO[c.L.jeito] : "EQUILIBRADO") + "  \u00B7  " +
                       (c.L.gesto >= 0 && c.L.gesto < 6 ? MOTOR[c.L.gesto] : "-");
        SizeF s3 = g.MeasureString(jeito, fPeq);
        Texto(g, jeito, fPeq, VERDE_M, r.X + (r.Width - s3.Width) / 2, r.Y + 80);
        float yy = r.Y + 102;
        Texto(g, "ENERGIA " + (int)c.L.energia, fTitulo, VERDE_M, r.X, yy);
        Barra(g, r.X, yy + 14, r.Width, 6, c.L.energia / 100f, VERDE);
        yy += 28;
        for (int k = 0; k < 7; k++, yy += 16)
        {
            Texto(g, EMOCAO[k], fPeq, VERDE_M, r.X, yy);
            Barra(g, r.X + 92, yy + 3, r.Width - 132, 8, c.L.emo[k], k == hm - 1 ? VERDE : VERDE_M);
            Texto(g, ((int)(c.L.emo[k] * 100)).ToString() + "%", fPeq, VERDE_M, r.Right - 34, yy);
        }
    }

    void DesenharPersonalidade(Graphics g, float x, float y, float w, float h)
    {
        if (h < 80) return;
        RectangleF r = Painel(g, x, y, w, h, "PERSONALIDADE", "UNICA");
        float yy = r.Y + 2, meio = r.X + 92 + (r.Width - 100) / 2;
        float passo = Math.Min(17, (r.Height - 4) / 7);
        for (int k = 0; k < 7; k++, yy += passo)
        {
            Texto(g, JEITO[k], fPeq, VERDE_M, r.X, yy);
            float v = Math.Max(-1, Math.Min(1, c.L.traco[k])), larg = (r.Width - 100) / 2;
            using (SolidBrush b = new SolidBrush(VERDE_EE)) g.FillRectangle(b, r.X + 92, yy + 3, r.Width - 100, 8);
            using (SolidBrush b = new SolidBrush(v >= 0 ? VERDE : Color.FromArgb(255, 120, 90)))
                g.FillRectangle(b, v >= 0 ? meio : meio + v * larg, yy + 3, Math.Abs(v) * larg, 8);
            using (Pen p = new Pen(VERDE_M)) g.DrawLine(p, meio, yy + 1, meio, yy + 13);
        }
    }

    void Posicionar(RectangleF r)
    {
        // sentidos a esquerda, nuvem oculta no meio, emocoes e motores a direita, dopamina em cima
        for (int s = 0; s < 14; s++)
            for (int j = 0; j < 4; j++)
            {
                float yy = r.Y + 10 + (s + 0.5f) * (r.Height - 20) / 14;
                posNo[s * 4 + j] = new PointF(r.X + 92 + (j % 2) * 6, yy + (j / 2) * 6 - 3);
            }
        float cx = r.X + r.Width * 0.47f, cy = r.Y + r.Height * 0.5f;
        float rx = r.Width * 0.25f, ry = r.Height * 0.44f;
        Random q = new Random(266);
        for (int i = Cerebro.EXC0; i < Cerebro.EMO0; i++)
        {
            // cada oculto perto do estimulo que prefere (assembleias), com espalhamento
            double ang = q.NextDouble() * Math.PI * 2, raio = Math.Sqrt(q.NextDouble());
            float px = cx + (float)(Math.Cos(ang) * raio) * rx;
            float py = cy + (float)(Math.Sin(ang) * raio) * ry;
            if (i < Cerebro.INH0)
            {
                int pref = (i - Cerebro.EXC0) % 14;
                float alvo = r.Y + 10 + (pref + 0.5f) * (r.Height - 20) / 14;
                py = py * 0.55f + alvo * 0.45f;
            }
            posNo[i] = new PointF(px, py);
        }
        float xr = r.Right - 112;
        for (int k = 0; k < 7; k++)
            for (int j = 0; j < 4; j++)
                posNo[Cerebro.EMO0 + k * 4 + j] = new PointF(xr + (j % 2) * 6, r.Y + 28 + k * (r.Height * 0.52f / 7) + (j / 2) * 6);
        for (int k = 0; k < 6; k++)
            for (int j = 0; j < 3; j++)
                posNo[Cerebro.MOT0 + k * 3 + j] = new PointF(xr + j * 6, r.Y + r.Height * 0.6f + k * (r.Height * 0.36f / 6));
        for (int j = 0; j < 4; j++) posNo[Cerebro.DA0 + j] = new PointF(r.X + r.Width * 0.47f - 9 + j * 6, r.Y + 6);
    }

    void DesenharRede(Graphics g, float x, float y, float w, float h)
    {
        RectangleF r = Painel(g, x, y, w, h, "SISTEMA NERVOSO", Cerebro.N + " NEURONIOS \u00B7 " + c.NS + " SINAPSES");
        if (rRede != r) { rRede = r; Posicionar(r); versaoLinhas = -1; }
        if (c.NS == 0) return;
        // sinapses: bitmap com todas as linhas, redesenhado quando os pesos mudam
        if (versaoLinhas != c.VersaoPesos || linhas == null || tamLinhas != ClientSize)
        {
            if (linhas != null) linhas.Dispose();
            tamLinhas = ClientSize;
            linhas = new Bitmap(Math.Max(1, (int)r.Width), Math.Max(1, (int)r.Height), PixelFormat.Format32bppPArgb);
            using (Graphics lg = Graphics.FromImage(linhas))
            {
                lg.SmoothingMode = SmoothingMode.AntiAlias;
                lg.TranslateTransform(-r.X, -r.Y);
                for (int k = 0; k < c.NS; k++)
                {
                    float wv = c.W[k];
                    int a = (int)Math.Min(60, Math.Abs(wv) * (c.Plastica[k] != 0 ? 40 : 14));
                    if (a < 3) continue;
                    Color cor = wv < 0 ? Color.FromArgb(a, 255, 140, 70) : Color.FromArgb(a, c.Plastica[k] != 0 ? VERDE : VERDE_M);
                    using (Pen p = new Pen(cor, c.Plastica[k] != 0 ? 1.0f : 0.6f)) lg.DrawLine(p, posNo[c.Pre[k]], posNo[c.Pos[k]]);
                }
            }
            versaoLinhas = c.VersaoPesos;
        }
        g.DrawImageUnscaled(linhas, (int)r.X, (int)r.Y);

        // aprendizado: sinapse que acabou de mudar pisca (dourado = fortaleceu, vermelho = enfraqueceu)
        int tick = Environment.TickCount;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        int mostradas = 0;
        for (int k = 0; k < c.NS && mostradas < 900; k++)
        {
            if (c.Muda[k] == 0) continue;
            int idade = tick - c.TMuda[k];
            if (idade > 3000) { c.Muda[k] = 0; continue; }
            float f = 1f - idade / 3000f, m = Math.Abs(c.Muda[k]);
            Color cor = c.Muda[k] > 0 ? Color.FromArgb((int)(220 * f), 255, 214, 90) : Color.FromArgb((int)(220 * f), 255, 90, 90);
            using (Pen p = new Pen(cor, 1.2f + 2.2f * m * f)) g.DrawLine(p, posNo[c.Pre[k]], posNo[c.Pos[k]]);
            mostradas++;
        }
        g.SmoothingMode = SmoothingMode.None;

        // disparos recentes: o neuronio acende e as sinapses de saida brilham
        long agora = c.UltimoPasso;
        float[] luz = new float[Cerebro.N];
        for (int d = 0; d < 60 && agora - d >= 0; d++)
            for (int i = 0; i < Cerebro.N; i++)
                if (luz[i] == 0 && c.Disparou(agora - d, i)) luz[i] = 1f - d / 60f;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        int acesos = 0;
        for (int k = 0; k < c.NS && acesos < 900; k++)
        {
            float l = luz[c.Pre[k]];
            if (l < 0.55f || Math.Abs(c.W[k]) < 0.15f) continue;
            acesos++;
            Color cor = c.W[k] < 0 ? Color.FromArgb((int)(150 * l), 255, 150, 80) : Color.FromArgb((int)(170 * l), VERDE);
            using (Pen p = new Pen(cor, 1.2f)) g.DrawLine(p, posNo[c.Pre[k]], posNo[c.Pos[k]]);
        }
        // sinais eletricos viajando: cada disparo manda um pulso por suas sinapses mais fortes
        const int VIAGEM = 160;
        int pulsos = 0;
        using (SolidBrush bExc = new SolidBrush(Color.FromArgb(235, 200, 255, 210)))
        using (SolidBrush bInh = new SolidBrush(Color.FromArgb(235, 255, 170, 100)))
        using (SolidBrush halo = new SolidBrush(Color.FromArgb(60, VERDE)))
            for (int d = 0; d < VIAGEM && agora - d >= 0 && pulsos < 2200; d += 2)
                for (int i = 0; i < Cerebro.N && pulsos < 2200; i++)
                {
                    if (!c.Disparou(agora - d, i) && !c.Disparou(agora - d - 1, i)) continue;
                    float t = d / (float)VIAGEM;
                    foreach (int k in c.Saida[i])
                    {
                        PointF a = posNo[i], b = posNo[c.Pos[k]];
                        float px = a.X + (b.X - a.X) * t, py = a.Y + (b.Y - a.Y) * t;
                        if (c.W[k] >= 0.5f) g.FillEllipse(halo, px - 3.5f, py - 3.5f, 7, 7);
                        g.FillRectangle(c.W[k] < 0 ? bInh : bExc, px - 1.2f, py - 1.2f, 2.4f, 2.4f);
                        pulsos++;
                    }
                }

        for (int i = 0; i < Cerebro.N; i++)
        {
            Color cor = COR_POP[Cerebro.Populacao(i)];
            float l = luz[i], raio = Cerebro.Populacao(i) == 1 || Cerebro.Populacao(i) == 2 ? 2.2f : 2.6f;
            if (l > 0)
                using (SolidBrush b = new SolidBrush(Color.FromArgb((int)(90 * l), cor)))
                    g.FillEllipse(b, posNo[i].X - raio * 3, posNo[i].Y - raio * 3, raio * 6, raio * 6);
            using (SolidBrush b = new SolidBrush(l > 0 ? cor : Color.FromArgb(110, cor)))
                g.FillEllipse(b, posNo[i].X - raio, posNo[i].Y - raio, raio * 2, raio * 2);
            if (i == c.Vigiado)
                using (Pen p = new Pen(Color.White, 1.2f)) g.DrawEllipse(p, posNo[i].X - 6, posNo[i].Y - 6, 12, 12);
        }
        g.SmoothingMode = SmoothingMode.None;
        // rotulos
        for (int s = 0; s < 14; s++)
        {
            SizeF t = g.MeasureString(SENSOR[s], fPeq);
            Texto(g, SENSOR[s], fPeq, c.L.sens[s] > 0.05f ? VERDE : VERDE_M, posNo[s * 4].X - t.Width - 6, posNo[s * 4].Y - 4);
        }
        for (int k = 0; k < 7; k++)
            Texto(g, EMOCAO[k], fPeq, c.L.emo[k] > 0.3f ? VERDE : VERDE_M, posNo[Cerebro.EMO0 + k * 4].X + 16, posNo[Cerebro.EMO0 + k * 4].Y - 2);
        for (int k = 0; k < 6; k++)
            Texto(g, MOTOR[k], fPeq, c.L.mot[k] > 0.3f ? Color.FromArgb(150, 235, 255) : Color.FromArgb(70, 160, 190),
                  posNo[Cerebro.MOT0 + k * 3].X + 22, posNo[Cerebro.MOT0 + k * 3].Y - 5);
        Texto(g, "DOPAMINA", fPeq, COR_POP[5], posNo[Cerebro.DA0 + 3].X + 10, posNo[Cerebro.DA0].Y - 6);
        // aviso de aprendizado
        int desde = tick - c.AprendeuEm;
        if (desde < 3500)
        {
            float f = desde < 3000 ? 1f : 1f - (desde - 3000) / 500f;
            string t = "APRENDENDO \u00B7 " + c.Mudaram + " SINAPSES MUDARAM";
            Brilho(g, t, fMed, Color.FromArgb((int)(255 * f), 255, 214, 90), r.X + 4, r.Y + 2);
        }
        string av = Fonte.Aviso;
        if (av != null)
        {
            string[] ls = av.Split('\n');
            SizeF s1 = g.MeasureString(ls[0], fMed), s2 = ls.Length > 1 ? g.MeasureString(ls[1], fPeq) : SizeF.Empty;
            float bw = Math.Max(s1.Width, s2.Width) + 30, bh = 56;
            float bx = r.X + (r.Width - bw) / 2, by = r.Y + (r.Height - bh) / 2;
            using (SolidBrush b = new SolidBrush(Color.FromArgb(235, 6, 10, 7))) g.FillRectangle(b, bx, by, bw, bh);
            using (Pen p = new Pen(Color.FromArgb(255, 200, 80), 2)) g.DrawRectangle(p, bx, by, bw, bh);
            Brilho(g, ls[0], fMed, Color.FromArgb(255, 214, 90), bx + (bw - s1.Width) / 2, by + 10);
            if (ls.Length > 1) Texto(g, ls[1], fPeq, VERDE_M, bx + (bw - s2.Width) / 2, by + 32);
        }
        // legenda
        float lx = r.X + 120, ly = r.Bottom - 12;
        for (int p = 0; p < 6; p++)
        {
            using (SolidBrush b = new SolidBrush(COR_POP[p])) g.FillEllipse(b, lx, ly + 2, 6, 6);
            Texto(g, NOME_POP[p], fPeq, VERDE_M, lx + 8, ly - 2);
            lx += g.MeasureString(NOME_POP[p], fPeq).Width + 20;
        }
    }

    void DesenharRaster(Graphics g, float x, float y, float w, float h)
    {
        RectangleF r = Painel(g, x, y, w, h, "DISPAROS (PRINT REAL)", c.L.disparos + "/s \u00B7 OCULTO " + c.L.taxa.ToString("0.0") + " Hz");
        rRaster = r;
        int cols = Math.Max(1, (int)r.Width - 70), ms = 2000;
        using (Bitmap bmp = new Bitmap(cols, Cerebro.N, PixelFormat.Format32bppRgb))
        {
            BitmapData d = bmp.LockBits(new Rectangle(0, 0, cols, Cerebro.N), ImageLockMode.WriteOnly, PixelFormat.Format32bppRgb);
            int[] px = new int[cols * Cerebro.N];
            int fundo = PAINEL.ToArgb();
            for (int i = 0; i < px.Length; i++) px[i] = fundo;
            long fim = c.UltimoPasso;
            for (int col = 0; col < cols; col++)
            {
                long p0 = fim - ms + (long)col * ms / cols, p1 = fim - ms + (long)(col + 1) * ms / cols;
                for (long p = p0; p < p1; p++)
                {
                    if (p < 0 || fim - p >= Cerebro.HIST) continue;
                    int o = (int)(p % Cerebro.HIST) * Cerebro.BYTES;
                    for (int by = 0; by < Cerebro.BYTES; by++)
                    {
                        int v = c.Hist[o + by];
                        if (v == 0) continue;
                        for (int bit = 0; bit < 8; bit++)
                        {
                            int i = by * 8 + bit;
                            if ((v & (1 << bit)) != 0 && i < Cerebro.N) px[i * cols + col] = COR_POP[Cerebro.Populacao(i)].ToArgb();
                        }
                    }
                }
            }
            Marshal.Copy(px, 0, d.Scan0, px.Length);
            bmp.UnlockBits(d);
            g.InterpolationMode = InterpolationMode.NearestNeighbor;
            g.PixelOffsetMode = PixelOffsetMode.Half;
            g.DrawImage(bmp, new RectangleF(r.X + 70, r.Y, cols, r.Height - 12));
            g.PixelOffsetMode = PixelOffsetMode.Default;
        }
        int[] ini = { 0, Cerebro.EXC0, Cerebro.INH0, Cerebro.EMO0, Cerebro.MOT0, Cerebro.DA0, Cerebro.N };
        float esc = (r.Height - 12) / Cerebro.N;
        for (int p = 0; p < 6; p++)
        {
            float y0 = r.Y + ini[p] * esc, y1 = r.Y + ini[p + 1] * esc;
            using (Pen pen = new Pen(VERDE_EE)) g.DrawLine(pen, r.X + 70, y0, r.Right, y0);
            if (y1 - y0 >= 9) Texto(g, NOME_POP[p], fPeq, COR_POP[p], r.X, (y0 + y1) / 2 - 6);
        }
        Texto(g, "-2 s", fPeq, VERDE_M, r.X + 70, r.Bottom - 12);
        Texto(g, "agora", fPeq, VERDE_M, r.Right - 36, r.Bottom - 12);
    }

    void DesenharMembrana(Graphics g, float x, float y, float w, float h)
    {
        int i = c.Vigiado;
        string[] pops = { "SENSORIAL", "OCULTO", "INIBITORIO", "EMOCAO", "MOTOR", "DOPAMINA" };
        RectangleF r = Painel(g, x, y, w, h, "NEURONIO #" + i + " \u00B7 " + pops[Cerebro.Populacao(i)], "LIF");
        rMembrana = r;
        Texto(g, "POTENCIAL DE MEMBRANA", fPeq, VERDE_M, r.X, r.Y);
        float v = c.V[i];
        Brilho(g, "V " + v.ToString("0.00"), fGrande, VERDE, r.Right - 92, r.Y - 4);
        float gy0 = r.Y + 40, gy1 = r.Bottom - 34, gh = gy1 - gy0;
        // escala: repouso (0) embaixo, limiar (1) a 70% da altura
        Func<float, float> Y = delegate(float val) { return gy1 - Math.Max(-0.2f, Math.Min(1.4f, val)) / 1.4f * gh; };
        using (Pen p = new Pen(VERDE_M) { DashStyle = DashStyle.Dash }) g.DrawLine(p, r.X, Y(c.Limiar[i]), r.Right, Y(c.Limiar[i]));
        Texto(g, "LIMIAR", fPeq, VERDE_M, r.Right - 50, Y(c.Limiar[i]) - 14);
        using (Pen p = new Pen(VERDE_EE)) g.DrawLine(p, r.X, Y(0), r.Right, Y(0));
        Texto(g, "REPOUSO", fPeq, VERDE_E, r.X, Y(0) + 2);
        // traco: ultimos ~1,5 s
        int ms = 1500;
        long fim = c.UltimoPasso;
        List<PointF> pts = new List<PointF>();
        bool disparouAgora = false;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using (Pen pv = new Pen(VERDE, 1.6f))
        using (Pen ps = new Pen(VERDE, 1.6f))
        {
            float ultimoV = 0;
            for (long p = fim - ms; p <= fim; p++)
            {
                if (p < 0 || fim - p >= Cerebro.HIST) continue;
                float xx = r.X + (float)(p - (fim - ms)) / ms * r.Width;
                float vv = c.TracoV[(int)(p % Cerebro.HIST)];
                if (c.Disparou(p, i))
                {
                    if (pts.Count > 1) g.DrawLines(pv, pts.ToArray());
                    pts.Clear();
                    g.DrawLine(ps, xx, Y(ultimoV), xx, gy0 - 6);     // o disparo sobe ate em cima
                    g.DrawLine(ps, xx, gy0 - 6, xx, Y(0));
                    if (fim - p < 300) disparouAgora = true;
                    ultimoV = 0;
                    continue;
                }
                if (float.IsNaN(vv)) continue;
                ultimoV = vv;
                pts.Add(new PointF(xx, Y(vv)));
            }
            if (pts.Count > 1) g.DrawLines(pv, pts.ToArray());
        }
        g.SmoothingMode = SmoothingMode.None;
        if (disparouAgora) Brilho(g, "DISPAROU", fGrande, VERDE, r.X + 6, r.Y + 14);
        // sinais eletricos: disparos que chegaram neste neuronio nos ultimos 100 ms
        int sinais = 0;
        for (int k = 0; k < c.NS; k++)
        {
            if (c.Pos[k] != i) continue;
            for (long p = fim - 100; p <= fim; p++) if (c.Disparou(p, c.Pre[k])) { sinais++; break; }
        }
        Texto(g, "\u25B2 SINAIS ELETRICOS \u00D7 " + sinais, fPeq, VERDE_M, r.X, r.Bottom - 14);
        Texto(g, "clique num neuronio", fPeq, VERDE_E, r.Right - 118, r.Bottom - 14);
    }

    void DesenharEstimulos(Graphics g, float x, float y, float w, float h)
    {
        RectangleF r = Painel(g, x, y, w, h, "ESTIMULOS", "INPUT");
        float yy = r.Y;
        float passo = 15;
        for (int s = 0; s < 14; s++, yy += passo)
        {
            Texto(g, SENSOR[s], fPeq, c.L.sens[s] > 0.05f ? VERDE : VERDE_M, r.X, yy);
            Barra(g, r.X + 76, yy + 3, r.Width - 76, 8, c.L.sens[s], VERDE);
        }
        yy += 8;
        // botoes em duas colunas
        float bw = (r.Width - 6) / 2, bh = 24;
        botoes[15].Texto = "VELOC. x" + Velocidade;
        for (int k = 0; k < botoes.Count; k++)
        {
            Botao b = botoes[k];
            b.R = new RectangleF(r.X + (k % 2) * (bw + 6), yy + (k / 2) * (bh + 5), bw, bh);
            bool aceso = (b.Aceso != null && b.Aceso()) || (DateTime.Now - b.Flash).TotalMilliseconds < 250;
            using (SolidBrush f = new SolidBrush(aceso ? VERDE_E : VERDE_EE)) g.FillRectangle(f, b.R);
            using (Pen p = new Pen(aceso ? VERDE : VERDE_M)) g.DrawRectangle(p, b.R.X, b.R.Y, b.R.Width, b.R.Height);
            SizeF s = g.MeasureString(b.Texto, fBotao);
            Texto(g, b.Texto, fBotao, aceso ? VERDE : Color.FromArgb(180, 255, 200), b.R.X + (b.R.Width - s.Width) / 2, b.R.Y + (bh - s.Height) / 2);
        }
        float yEst = yy + ((botoes.Count + 1) / 2) * (bh + 5) + 4;
        if (yEst < r.Bottom - 12) Texto(g, Fonte.Estado, fPeq, VERDE_E, r.X, yEst);
        if (DemoAgora != "" && yEst + 16 < r.Bottom - 12) Texto(g, "DEMO: " + DemoAgora, fMed, VERDE, r.X, yEst + 16);
    }

    void DesenharDopamina(Graphics g, float x, float y, float w, float h)
    {
        RectangleF r = Painel(g, x, y, w, h, "DOPAMINA \u00B7 PLASTICIDADE", "RECOMPENSA");
        Texto(g, "recompensas " + c.L.recompensas + "  \u00B7  broncas " + c.L.punicoes + "  \u00B7  licoes do LLM " + c.L.licoes, fPeq, VERDE_M, r.X, r.Y);
        Texto(g, "sinapses que ja mudaram nesta sessao: " + c.MudancasTotal, fPeq, Color.FromArgb(255, 214, 90), r.X, r.Y + 13);
        float gy0 = r.Y + 18, gy1 = r.Bottom - 30, meio = (gy0 + gy1) / 2;
        using (Pen p = new Pen(VERDE_EE)) g.DrawLine(p, r.X, meio, r.Right, meio);
        List<PointF> da = new List<PointF>(), pw = new List<PointF>();
        int n = histDa.Length;
        for (int j = 0; j < n; j++)
        {
            int idx = (iHist + j) % n;
            float xx = r.X + (float)j / (n - 1) * r.Width;
            da.Add(new PointF(xx, meio - histDa[idx] * (gy1 - gy0) / 2));
            pw.Add(new PointF(xx, gy1 - Math.Min(1, histPeso[idx] / 1.2f) * (gy1 - gy0)));
        }
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using (Pen p = new Pen(Color.FromArgb(120, 90, 225, 255), 1.2f)) g.DrawLines(p, pw.ToArray());
        using (Pen p = new Pen(COR_POP[5], 1.6f)) g.DrawLines(p, da.ToArray());
        g.SmoothingMode = SmoothingMode.None;
        Texto(g, "dopamina " + c.L.dopamina.ToString("+0.00;-0.00"), fPeq, COR_POP[5], r.X, r.Bottom - 26);
        Texto(g, "peso medio das sinapses que aprendem " + c.L.peso_medio.ToString("0.000"), fPeq, Color.FromArgb(90, 225, 255), r.X, r.Bottom - 13);
        double tr = (DateTime.Now - flashRec).TotalMilliseconds, tp = (DateTime.Now - flashPun).TotalMilliseconds;
        double tl = (DateTime.Now - flashLic).TotalMilliseconds;
        string aviso = tr < 1500 ? "+1 RECOMPENSA" : tp < 1500 ? "-1 BRONCA" : tl < 1500 ? "LLM ENSINOU" : null;
        if (aviso != null)
        {
            SizeF s = g.MeasureString(aviso, fGrande);
            float bx = r.X + (r.Width - s.Width) / 2 - 8, by = meio - s.Height / 2 - 4;
            using (SolidBrush b = new SolidBrush(Color.FromArgb(230, 6, 12, 8))) g.FillRectangle(b, bx, by, s.Width + 16, s.Height + 8);
            using (Pen p = new Pen(tp < 1500 && tr >= 1500 ? Color.FromArgb(255, 120, 90) : VERDE, 2)) g.DrawRectangle(p, bx, by, s.Width + 16, s.Height + 8);
            Brilho(g, aviso, fGrande, tp < 1500 && tr >= 1500 ? Color.FromArgb(255, 140, 110) : VERDE, bx + 8, by + 4);
        }
    }

    // ---------------- mouse ----------------

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        foreach (Botao b in botoes)
            if (b.R.Contains(e.Location)) { b.Flash = DateTime.Now; b.Clique(); return; }
        if (rRede.Contains(e.Location))
        {
            int melhor = -1;
            float dist = 14 * 14;
            for (int i = 0; i < Cerebro.N; i++)
            {
                float dx = posNo[i].X - e.X, dy = posNo[i].Y - e.Y;
                if (dx * dx + dy * dy < dist) { dist = dx * dx + dy * dy; melhor = i; }
            }
            if (melhor >= 0) lock (c) { c.Vigiado = melhor; for (int k = 0; k < c.TracoV.Length; k++) c.TracoV[k] = float.NaN; }
        }
    }
}

internal sealed class Janela : Form
{
    readonly Cerebro c = new Cerebro();
    internal readonly Tela tela;
    readonly Timer timer = new Timer();
    readonly TextBox ip = new TextBox();
    readonly Button modo = new Button();
    FonteSim sim;
    FonteRobo robo;
    DateTime ultimo = DateTime.Now, salvo = DateTime.Now;
    static readonly string CFG = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ZeusCerebro.txt");

    public Janela()
    {
        Text = "Zeus \u00B7 cerebro (SNN 266 neuronios) \u00B7 rosto + rede neural em acao";
        BackColor = Color.FromArgb(4, 7, 5);
        ClientSize = new Size(1600, 900);
        MinimumSize = new Size(1100, 700);
        StartPosition = FormStartPosition.CenterScreen;
        KeyPreview = true;

        Panel barra = new Panel();
        barra.Dock = DockStyle.Top;
        barra.Height = 30;
        barra.BackColor = Color.FromArgb(8, 14, 10);
        Label l = new Label();
        l.Text = "Robo (IP):";
        l.ForeColor = Color.FromArgb(52, 170, 80);
        l.Font = new Font("Consolas", 9f, FontStyle.Bold);
        l.AutoSize = true;
        l.Location = new Point(10, 7);
        ip.Location = new Point(96, 4);
        ip.Width = 140;
        ip.BackColor = Color.FromArgb(14, 36, 20);
        ip.ForeColor = Color.FromArgb(92, 255, 122);
        ip.BorderStyle = BorderStyle.FixedSingle;
        ip.Font = new Font("Consolas", 9.5f);
        ip.Text = "192.168.137.201";
        try { if (File.Exists(CFG)) ip.Text = File.ReadAllText(CFG).Trim(); } catch (IOException) { }
        modo.Location = new Point(246, 2);
        modo.Size = new Size(220, 25);
        modo.FlatStyle = FlatStyle.Flat;
        modo.FlatAppearance.BorderColor = Color.FromArgb(52, 170, 80);
        modo.ForeColor = Color.FromArgb(92, 255, 122);
        modo.Font = new Font("Consolas", 9f, FontStyle.Bold);
        modo.Click += delegate { TrocarModo(); };
        Label dica = new Label();
        dica.Text = "SIMULACAO roda o firmware aqui no PC  \u00B7  AO VIVO mostra o cerebro real do ESP32  \u00B7  F11 tela cheia";
        dica.ForeColor = Color.FromArgb(40, 120, 60);
        dica.Font = new Font("Consolas", 8.5f);
        dica.AutoSize = true;
        dica.Location = new Point(480, 8);
        barra.Controls.Add(l);
        barra.Controls.Add(ip);
        barra.Controls.Add(modo);
        barra.Controls.Add(dica);

        sim = new FonteSim(c);
        tela = new Tela(c);
        tela.Fonte = sim;
        tela.Dock = DockStyle.Fill;
        Controls.Add(tela);
        Controls.Add(barra);
        modo.Text = "MODO: SIMULACAO  \u2192 ao vivo";

        timer.Interval = 33;
        timer.Tick += delegate
        {
            int ms = (int)Math.Min(100, (DateTime.Now - ultimo).TotalMilliseconds);
            ultimo = DateTime.Now;
            tela.Tick(ms);
            if (sim != null && (DateTime.Now - salvo).TotalSeconds > 60) { sim.Salvar(); salvo = DateTime.Now; }
        };
        timer.Start();
        KeyDown += delegate(object s, KeyEventArgs e)
        {
            if (e.KeyCode == Keys.F11) TelaCheia(WindowState != FormWindowState.Maximized || FormBorderStyle != FormBorderStyle.None);
            else if (e.KeyCode == Keys.Escape) TelaCheia(false);
        };
        FormClosing += delegate { if (sim != null) sim.Salvar(); if (robo != null) robo.Parar(); };
    }

    void TelaCheia(bool on)
    {
        FormBorderStyle = on ? FormBorderStyle.None : FormBorderStyle.Sizable;
        WindowState = on ? FormWindowState.Maximized : FormWindowState.Normal;
    }

    void TrocarModo()
    {
        lock (c)
        {
            if (robo == null)
            {
                if (sim != null) sim.Salvar();
                try { File.WriteAllText(CFG, ip.Text.Trim()); } catch (IOException) { }
                c.UltimoPasso = -1;
                robo = new FonteRobo(c, ip.Text);
                tela.Fonte = robo;
                sim = null;
                modo.Text = "MODO: ROBO AO VIVO  \u2192 simulacao";
            }
            else
            {
                robo.Parar();
                robo = null;
                c.UltimoPasso = -1;
                Array.Clear(c.Hist, 0, c.Hist.Length);
                sim = new FonteSim(c);
                tela.Fonte = sim;
                modo.Text = "MODO: SIMULACAO  \u2192 ao vivo";
            }
        }
    }

    [STAThread]
    static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Janela j = new Janela();
        if (Array.IndexOf(args, "--demo") >= 0) j.tela.IniciarDemo();     // abre ja rodando a demo
        Application.Run(j);
    }
}
