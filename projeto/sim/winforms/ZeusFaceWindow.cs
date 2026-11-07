// Rosto do Zeus numa janela do Windows: espelho ao vivo do OLED do robo.
//
// Os pixels sao os reais: a janela le o framebuffer do SSD1306 pela rota /frame do
// ESP32 (1024 bytes) com dois pedidos em paralelo numa conexao reaproveitada, tudo
// assincrono (a janela nunca trava esperando a rede). O endereco do robo vem da ponte
// (/app/cfg, que sabe o IP pelo canal da internet) ou de uma busca na rede do hotspot
// do PC (192.168.137.x). Os comandos (expressao, texto, boca) vao pela ponte, que os
// entrega pelo canal, WiFi de casa ou cabo USB; sem ponte, direto no robo.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

// O OLED ampliado. O SSD1306 do robo e de duas cores: as 16 linhas de cima sao amarelas.
internal sealed class OledView : Control
{
    const int W = 128, H = 64, FAIXA = 16;
    const int AMARELO = unchecked((int)0xFFFFD23F), AZUL = unchecked((int)0xFF4FC3F7), APAGADO = unchecked((int)0xFF06080C);
    readonly Bitmap bmp = new Bitmap(W, H, PixelFormat.Format32bppRgb);
    readonly int[] px = new int[W * H];
    bool grade = true;

    public OledView()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        BackColor = Color.Black;
        for (int i = 0; i < px.Length; i++) px[i] = APAGADO;
        Copiar();
    }

    public bool Grade { get { return grade; } set { grade = value; Invalidate(); } }

    // framebuffer do SSD1306: 8 paginas de 128 bytes; cada byte e uma coluna de 8 pixels
    public void Mostrar(byte[] fb)
    {
        for (int y = 0; y < H; y++)
        {
            int pagina = (y >> 3) * W, bit = 1 << (y & 7), aceso = y < FAIXA ? AMARELO : AZUL, linha = y * W;
            for (int x = 0; x < W; x++) px[linha + x] = (fb[pagina + x] & bit) != 0 ? aceso : APAGADO;
        }
        Copiar();
        Invalidate();
    }

    void Copiar()
    {
        BitmapData d = bmp.LockBits(new Rectangle(0, 0, W, H), ImageLockMode.WriteOnly, PixelFormat.Format32bppRgb);
        try { Marshal.Copy(px, 0, d.Scan0, px.Length); }      // 32 bpp: cada linha tem exatamente W * 4 bytes
        finally { bmp.UnlockBits(d); }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        Graphics g = e.Graphics;
        g.Clear(Color.Black);
        int esc = Math.Max(1, Math.Min(ClientSize.Width / W, ClientSize.Height / H));   // escala inteira: pixels nitidos
        int w = esc * W, h = esc * H, x0 = (ClientSize.Width - w) / 2, y0 = (ClientSize.Height - h) / 2;
        g.InterpolationMode = InterpolationMode.NearestNeighbor;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        g.DrawImage(bmp, new Rectangle(x0, y0, w, h));
        if (!grade || esc < 4) return;
        g.PixelOffsetMode = PixelOffsetMode.Default;           // vao entre os pixels, como no OLED de verdade
        using (var p = new Pen(Color.Black))
        {
            for (int i = 0; i <= W; i++) g.DrawLine(p, x0 + i * esc, y0, x0 + i * esc, y0 + h);
            for (int j = 0; j <= H; j++) g.DrawLine(p, x0, y0 + j * esc, x0 + w, y0 + j * esc);
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) bmp.Dispose();
        base.Dispose(disposing);
    }
}

internal sealed class FaceForm : Form
{
    const string AUTO = "auto";
    const string PONTE = "http://127.0.0.1:8770";
    const int FAIXAS = 2;                       // pedidos de /frame em paralelo (cada um leva ~80 ms no ESP32)
    static readonly string[] Humores = { "neutro", "feliz", "triste", "bravo", "sono", "curioso", "surpreso", "pensando" };
    static readonly Color Fundo = Color.FromArgb(12, 14, 18), Barra = Color.FromArgb(24, 28, 36),
                          Botao = Color.FromArgb(38, 44, 56), Destaque = Color.FromArgb(255, 210, 63);

    readonly HttpClient http;
    readonly string token = AcharToken();
    readonly string arquivoAjustes = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ZeusOled.txt");

    readonly OledView oled = new OledView { Dock = DockStyle.Fill };
    readonly FlowLayoutPanel topo = new FlowLayoutPanel();
    readonly StatusStrip rodape = new StatusStrip();
    readonly ToolStripStatusLabel lblLigado = new ToolStripStatusLabel("●"), lblVelocidade = new ToolStripStatusLabel(),
                                 lblMsg = new ToolStripStatusLabel { Spring = true, TextAlign = ContentAlignment.MiddleLeft };
    readonly TextBox txtEndereco = new TextBox { Width = 170 }, txtTexto = new TextBox { Width = 190 };
    readonly CheckBox chkFala = new CheckBox(), chkGrade = new CheckBox();
    readonly Button[] btnHumor = new Button[Humores.Length];
    readonly System.Windows.Forms.Timer relogio = new System.Windows.Forms.Timer { Interval = 1000 };

    CancellationTokenSource sessao;
    string robo;                                 // http://ip do robo em uso
    long pedidos, mostrado;                      // numeracao dos quadros: nunca volta para um mais velho
    byte[] ultimo;
    int quadros, mudancas, falhasSeguidas;
    double latenciaMs;
    string humorAtual;
    bool telaCheia, buscando;
    FormBorderStyle bordaSalva;
    FormWindowState estadoSalvo;

    public FaceForm()
    {
        ServicePointManager.DefaultConnectionLimit = 8;
        ServicePointManager.Expect100Continue = false;
        ServicePointManager.UseNagleAlgorithm = false;
        http = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
        http.DefaultRequestHeaders.CacheControl = new System.Net.Http.Headers.CacheControlHeaderValue { NoCache = true };

        Text = "Zeus — OLED ao vivo";
        Icon = SystemIcons.Application;
        AutoScaleMode = AutoScaleMode.Dpi;
        Font = new Font("Segoe UI", 9.75f);
        BackColor = Fundo;
        ForeColor = Color.White;
        MinimumSize = new Size(420, 320);
        Size = new Size(980, 620);
        StartPosition = FormStartPosition.CenterScreen;
        KeyPreview = true;

        MontarTopo();
        MontarRodape();
        oled.DoubleClick += delegate { AlternarTelaCheia(); };
        Controls.Add(oled);
        Controls.Add(topo);
        Controls.Add(rodape);

        relogio.Tick += delegate { UmSegundo(); };
        KeyDown += Teclas;
        Shown += delegate { LerAjustes(); relogio.Start(); Reconectar(); };
        FormClosing += delegate { SalvarAjustes(); if (sessao != null) sessao.Cancel(); };
    }

    // ---------------- interface ----------------

    void MontarTopo()
    {
        // Dock Top + AutoSize nao recalcula a altura quando os itens quebram de linha: a
        // altura sai de GetPreferredSize na largura atual (AjustarTopo, a cada Resize)
        topo.Dock = DockStyle.Top;
        topo.WrapContents = true;                 // janela estreita: os grupos descem para a linha de baixo
        Layout += delegate { AjustarTopo(); };    // inclui a escala de DPI e o primeiro Show
        topo.ControlAdded += delegate { AjustarTopo(); };
        topo.Padding = new Padding(8, 6, 8, 4);
        topo.BackColor = Barra;

        txtEndereco.Text = AUTO;
        txtEndereco.KeyDown += (s, e) => { if (e.KeyCode == Keys.Enter) { e.SuppressKeyPress = true; Reconectar(); } };
        Grupo(Rotulo("Robô"), txtEndereco, NovoBotao("Conectar", delegate { Reconectar(); }));

        for (int i = 0; i < Humores.Length; i++)       // soltos na barra: cada botao desce de linha se faltar espaco
        {
            int m = i;
            btnHumor[i] = NovoBotao((i + 1) + " " + char.ToUpper(Humores[i][0]) + Humores[i].Substring(1),
                                    delegate { Enviar("/mood?m=" + m); MarcarHumor(Humores[m]); });
            btnHumor[i].Margin = new Padding(0, 0, 3, 4);
            topo.Controls.Add(btnHumor[i]);
        }
        topo.SetFlowBreak(btnHumor[Humores.Length - 1], false);

        txtTexto.KeyDown += (s, e) => { if (e.KeyCode == Keys.Enter) { e.SuppressKeyPress = true; ExibirTexto(); } };
        Grupo(txtTexto, NovoBotao("Exibir", delegate { ExibirTexto(); }));

        chkFala.Text = "Falando";
        chkFala.CheckedChanged += delegate { Enviar(chkFala.Checked ? "/talk?on=1&p=1" : "/talk?on=0"); };
        chkGrade.Text = "Grade";
        chkGrade.Checked = true;
        chkGrade.CheckedChanged += delegate { oled.Grade = chkGrade.Checked; };
        foreach (var c in new[] { chkFala, chkGrade }) { c.AutoSize = true; c.Margin = new Padding(0, 5, 10, 0); }
        Grupo(chkFala, chkGrade, NovoBotao("Tela cheia (F11)", delegate { AlternarTelaCheia(); }));
    }

    void AjustarTopo()
    {
        if (!topo.Visible || ClientSize.Width <= 0) return;
        int h = topo.GetPreferredSize(new Size(ClientSize.Width, 0)).Height;
        if (h > 0 && h != topo.Height) topo.Height = h;
    }

    void Grupo(params Control[] itens)
    {
        var g = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 0, 12, 4) };
        foreach (var c in itens) { c.Margin = new Padding(0, 0, 4, 0); g.Controls.Add(c); }
        topo.Controls.Add(g);
    }

    static Label Rotulo(string t)
    {
        return new Label { Text = t, AutoSize = true, Padding = new Padding(0, 6, 0, 0), ForeColor = Color.FromArgb(150, 160, 180) };
    }

    static Button NovoBotao(string t, EventHandler clique)
    {
        var b = new Button { Text = t, AutoSize = true, FlatStyle = FlatStyle.Flat, BackColor = Botao, ForeColor = Color.White,
                             Cursor = Cursors.Hand, UseVisualStyleBackColor = false };
        b.FlatAppearance.BorderColor = Color.FromArgb(60, 68, 84);
        b.Click += clique;
        return b;
    }

    void MontarRodape()
    {
        rodape.BackColor = Barra;
        rodape.ForeColor = Color.FromArgb(190, 198, 214);
        rodape.SizingGrip = true;
        lblLigado.ForeColor = Color.Gray;
        rodape.Items.AddRange(new ToolStripItem[] { lblLigado, lblVelocidade, lblMsg });
    }

    void Mensagem(string t) { lblMsg.Text = t; }

    void Ligado(bool sim)
    {
        lblLigado.ForeColor = sim ? Color.FromArgb(61, 220, 151) : Color.FromArgb(255, 107, 107);
    }

    void MarcarHumor(string nome)
    {
        if (nome == humorAtual) return;
        humorAtual = nome;
        for (int i = 0; i < Humores.Length; i++)
        {
            bool ativo = Humores[i] == nome;
            btnHumor[i].BackColor = ativo ? Destaque : Botao;
            btnHumor[i].ForeColor = ativo ? Color.Black : Color.White;
        }
    }

    void ExibirTexto()
    {
        string t = txtTexto.Text.Trim();
        if (t.Length == 0) return;
        int m = Math.Max(0, Array.IndexOf(Humores, humorAtual ?? "feliz"));
        Enviar("/show?t=" + Uri.EscapeDataString(t) + "&m=" + m);
        txtTexto.SelectAll();
    }

    void Teclas(object sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.F11 || (e.KeyCode == Keys.Escape && telaCheia)) { AlternarTelaCheia(); e.Handled = true; return; }
        if (txtTexto.Focused || txtEndereco.Focused) return;
        int n = e.KeyCode - Keys.D1;
        if (n < 0 || n >= Humores.Length) n = e.KeyCode - Keys.NumPad1;
        if (n >= 0 && n < Humores.Length) { btnHumor[n].PerformClick(); e.Handled = true; }
    }

    void AlternarTelaCheia()
    {
        telaCheia = !telaCheia;
        SuspendLayout();
        topo.Visible = rodape.Visible = !telaCheia;
        if (telaCheia)
        {
            bordaSalva = FormBorderStyle; estadoSalvo = WindowState;
            FormBorderStyle = FormBorderStyle.None; WindowState = FormWindowState.Normal; WindowState = FormWindowState.Maximized;
        }
        else { FormBorderStyle = bordaSalva; WindowState = estadoSalvo; }
        ResumeLayout();
        AjustarTopo();
    }

    // ---------------- conexao ----------------

    async void Reconectar()
    {
        if (buscando) return;
        buscando = true;
        if (sessao != null) sessao.Cancel();
        Ligado(false);
        try
        {
            for (int volta = 0; ; volta++)
            {
                Mensagem(volta == 0 ? "procurando o Zeus..." : "não achei o Zeus; tentando de novo (confira se ele está ligado)");
                string achado = await Descobrir();
                if (achado != null) { Iniciar(achado); return; }
                await Task.Delay(3000);
                if (IsDisposed) return;
            }
        }
        finally { buscando = false; }
    }

    async Task<string> Descobrir()
    {
        string escrito = txtEndereco.Text.Trim();
        if (escrito.Length > 0 && !escrito.Equals(AUTO, StringComparison.OrdinalIgnoreCase))
        {
            string url = escrito.StartsWith("http", StringComparison.OrdinalIgnoreCase) ? escrito.TrimEnd('/') : "http://" + escrito.TrimEnd('/');
            return await TemFrame(url) ? url : null;
        }
        // 1) a ponte sabe o IP do robo pelo canal da internet
        if (token != null)
        {
            try
            {
                string cfg = await Texto(PONTE + "/app/cfg", true, 3000);
                Match m = Regex.Match(cfg, "\"ip\"\\s*:\\s*\"([0-9.]+)\"");
                if (m.Success && await TemFrame("http://" + m.Groups[1].Value)) return "http://" + m.Groups[1].Value;
            }
            catch (Exception) { }
        }
        // 2) vizinhos na tabela ARP (hotspot do PC, rede de casa), todos ao mesmo tempo
        var testes = new List<Task<string>>();
        foreach (string ip in await Task.Run(() => VizinhosArp()))
        {
            string url = "http://" + ip;
            testes.Add(TemFrame(url).ContinueWith(t => t.Result ? url : null));
        }
        while (testes.Count > 0)
        {
            Task<string> pronto = await Task.WhenAny(testes);
            testes.Remove(pronto);
            if (pronto.Result != null) return pronto.Result;
        }
        return null;
    }

    static IEnumerable<string> VizinhosArp()
    {
        var ips = new List<string>();
        try
        {
            var p = Process.Start(new ProcessStartInfo("arp", "-a") { UseShellExecute = false, RedirectStandardOutput = true, CreateNoWindow = true });
            string saida = p.StandardOutput.ReadToEnd();
            p.WaitForExit(3000);
            foreach (Match m in Regex.Matches(saida, @"^\s+(\d+\.\d+\.\d+\.\d+)\s+([0-9a-f-]{17})\s+\w+", RegexOptions.Multiline))
            {
                string ip = m.Groups[1].Value;
                if (ip.EndsWith(".255") || ip.StartsWith("224.") || ip.StartsWith("239.") || m.Groups[2].Value == "ff-ff-ff-ff-ff-ff") continue;
                if (ip.StartsWith("192.168.137.")) ips.Insert(0, ip); else ips.Add(ip);   // hotspot do PC primeiro
            }
        }
        catch (Exception) { }
        return ips;
    }

    async Task<bool> TemFrame(string url)
    {
        try
        {
            using (var cts = new CancellationTokenSource(1500))
            using (HttpResponseMessage r = await http.GetAsync(url + "/frame", cts.Token))
                return r.IsSuccessStatusCode && (await r.Content.ReadAsByteArrayAsync()).Length == 1024;
        }
        catch (Exception) { return false; }
    }

    void Iniciar(string url)
    {
        robo = url;
        txtEndereco.Text = txtEndereco.Text.Trim().Equals(AUTO, StringComparison.OrdinalIgnoreCase) ? AUTO : url;
        sessao = new CancellationTokenSource();
        falhasSeguidas = 0;
        Ligado(true);
        Mensagem("conectado ao OLED em " + url + (token != null ? "  ·  comandos pela ponte" : "  ·  sem ponte: comandos direto no robô"));
        for (int i = 0; i < FAIXAS; i++) { var _ = Quadros(url + "/frame", sessao.Token, i * 40); }
    }

    // Busca quadros sem parar. Roda no contexto da janela: o await libera a tela enquanto a rede trabalha.
    async Task Quadros(string url, CancellationToken fim, int atraso)
    {
        await Task.Delay(atraso);                // as faixas defasadas: um quadro chega a cada ~40 ms
        while (!fim.IsCancellationRequested)
        {
            long meu = ++pedidos;
            long t0 = Stopwatch.GetTimestamp();
            byte[] fb = null;
            try
            {
                using (var cts = CancellationTokenSource.CreateLinkedTokenSource(fim))
                {
                    cts.CancelAfter(2500);
                    using (HttpResponseMessage r = await http.GetAsync(url, cts.Token))
                        if (r.IsSuccessStatusCode) fb = await r.Content.ReadAsByteArrayAsync();
                }
            }
            catch (Exception) { }
            if (fim.IsCancellationRequested) return;
            if (fb == null || fb.Length != 1024)
            {
                if (++falhasSeguidas >= 6) { Mensagem("o OLED parou de responder em " + url); Reconectar(); return; }
                await Task.Delay(Math.Min(2000, 150 * falhasSeguidas));
                continue;
            }
            falhasSeguidas = 0;
            double ms = (Stopwatch.GetTimestamp() - t0) * 1000.0 / Stopwatch.Frequency;
            latenciaMs = latenciaMs == 0 ? ms : latenciaMs * 0.8 + ms * 0.2;
            if (meu <= mostrado) continue;       // chegou depois de um mais novo: descarta
            mostrado = meu;
            quadros++;
            if (ultimo != null && Iguais(fb, ultimo)) continue;   // nada mudou: nao redesenha
            ultimo = fb;
            mudancas++;
            oled.Mostrar(fb);
        }
    }

    static bool Iguais(byte[] a, byte[] b)
    {
        for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    // ---------------- comandos e estado ----------------

    async Task<string> Texto(string url, bool comToken, int limiteMs)
    {
        using (var cts = new CancellationTokenSource(limiteMs))
        using (var pedido = new HttpRequestMessage(HttpMethod.Get, url))
        {
            if (comToken) pedido.Headers.Add("X-Zeus-Token", token);
            using (HttpResponseMessage r = await http.SendAsync(pedido, cts.Token))
            {
                r.EnsureSuccessStatusCode();
                return await r.Content.ReadAsStringAsync();
            }
        }
    }

    async void Enviar(string caminho)
    {
        if (token == null && robo == null) { Mensagem("conecte ao Zeus para mandar comandos"); return; }
        // a ponte entrega por onde der (canal da internet, WiFi, cabo USB); fora do ar: direto no robo
        if (token != null && await Tentar(PONTE + "/robo" + caminho, true, 4000)) return;
        if (robo != null && await Tentar(robo + caminho, false, 3000)) return;
        Mensagem("o comando não chegou ao Zeus");
    }

    async Task<bool> Tentar(string url, bool comToken, int limiteMs)
    {
        try { await Texto(url, comToken, limiteMs); return true; }
        catch (Exception) { return false; }
    }

    async void UmSegundo()
    {
        lblVelocidade.Text = robo == null ? "" : string.Format("{0} quadros/s  ·  {1} mudanças  ·  {2:0} ms", quadros, mudancas, latenciaMs);
        quadros = mudancas = 0;
        if (chkFala.Checked) Enviar("/talk?on=1");                 // a boca fecha sozinha sem este aviso
        if (robo == null) return;
        try
        {
            string estado = token != null ? await Texto(PONTE + "/robo/state", true, 2000) : await Texto(robo + "/state", false, 2000);
            Match m = Regex.Match(estado, "\"mood\"\\s*:\\s*\"(\\w+)\"");
            if (m.Success) MarcarHumor(m.Groups[1].Value);
        }
        catch (Exception) { }
    }

    // ---------------- ajustes ----------------

    static string AcharToken()
    {
        // procura ponte\zeus_ponte.json subindo a partir da pasta do programa (sim\winforms\bin\app)
        var pastas = new List<string>();
        for (var d = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory); d != null; d = d.Parent) pastas.Add(d.FullName);
        pastas.Add(@"D:\ROBO_Zeus");
        foreach (string p in pastas)
        {
            string arq = Path.Combine(p, "ponte", "zeus_ponte.json");
            if (!File.Exists(arq)) continue;
            Match m = Regex.Match(File.ReadAllText(arq), "\"token\"\\s*:\\s*\"([^\"]+)\"");
            if (m.Success) return m.Groups[1].Value;
        }
        return null;
    }

    void LerAjustes()
    {
        try
        {
            if (!File.Exists(arquivoAjustes)) return;
            string[] l = File.ReadAllLines(arquivoAjustes);
            if (l.Length > 0 && l[0].Trim().Length > 0) txtEndereco.Text = l[0].Trim();
            if (l.Length > 1) chkGrade.Checked = l[1].Trim() != "0";
        }
        catch (Exception) { }
    }

    void SalvarAjustes()
    {
        try { File.WriteAllLines(arquivoAjustes, new[] { txtEndereco.Text.Trim(), chkGrade.Checked ? "1" : "0" }); }
        catch (Exception) { }
    }
}

internal static class Program
{
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

    [STAThread]
    static void Main()
    {
        try { SetProcessDPIAware(); } catch (Exception) { }       // nitido em telas com escala de 125%/150%
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new FaceForm());
    }
}
