"""Zeus pela internet: https://zeus.appdevtest.shop (Cloudflare Tunnel APPDEVTEST -> esta ponte).

A ponte serve o app (a mesma pagina do ESP32), a pagina de downloads (/aplicativos) e repassa
os comandos do rosto ao robo na rede de casa (/robo/...). Assim o celular funciona em qualquer
rede (4G, outro WiFi), sem cabo e sem depender do isolamento do roteador.

Seguranca: quem chega pela internet precisa do CODIGO DE ACESSO (zeus_ponte.json) para receber
o token da ponte. Tentativas erradas bloqueiam o IP (e, em excesso, todo mundo) por um tempo.
"""
import json
import re
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PAGINA_APP = ROOT / "firmware" / "main" / "web" / "index.html"
SECRETS = ROOT / "firmware" / "main" / "secrets.h"
APLICATIVOS = HERE / "aplicativos"          # zeus.apk e icones (gerados pelo publicar)
ROBO_ROTAS = ("/state", "/mood", "/show", "/talk", "/spk", "/board", "/think", "/cfg", "/snn")
# estado da rede neural (SNN) que o robo manda junto: emocoes, energia, dopamina, jeito, humor
SNN_CAMPOS = ("e", "en", "da", "j", "h")

FALHAS_POR_IP = 5          # erros em JANELA_S -> esse IP bloqueado
FALHAS_TOTAL = 20          # erros de todo mundo em JANELA_S -> ninguem entra (forca bruta distribuida)
JANELA_S = 600

CABECA_PWA = (
    '<script>window.ZEUS_REMOTO=true</script>'
    '<link rel="apple-touch-icon" href="/apple-touch-icon.png">'
    '<meta name="apple-mobile-web-app-capable" content="yes">'
    '<meta name="mobile-web-app-capable" content="yes">'
    '<meta name="apple-mobile-web-app-status-bar-style" content="black">'
    '<meta name="apple-mobile-web-app-title" content="Zeus">'
)

MANIFEST = {
    "name": "Zeus", "short_name": "Zeus", "start_url": "/", "scope": "/", "display": "standalone",
    "background_color": "#0b0b0b", "theme_color": "#0b0b0b", "lang": "pt-BR",
    "icons": [{"src": "/icon-192.png", "sizes": "192x192", "type": "image/png"},
              {"src": "/icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any maskable"}],
}


class Bloqueado(Exception):
    pass


class Acesso:
    def __init__(self, codigo):
        self.codigo = str(codigo)
        self.lock = threading.Lock()
        self.falhas = {}          # ip -> [instantes]
        self.todas = []

    def verificar(self, ip, codigo):
        agora = time.time()
        with self.lock:
            self.todas = [t for t in self.todas if agora - t < JANELA_S]
            meu = [t for t in self.falhas.get(ip, []) if agora - t < JANELA_S]
            self.falhas[ip] = meu
            if len(meu) >= FALHAS_POR_IP or len(self.todas) >= FALHAS_TOTAL:
                raise Bloqueado("muitas tentativas erradas: espere 10 minutos")
            if str(codigo or "").strip() == self.codigo:
                self.falhas.pop(ip, None)
                return True
            meu.append(agora)
            self.todas.append(agora)
            return False


def pagina_app():
    html = PAGINA_APP.read_text(encoding="utf-8")
    return html.replace("<head>", "<head>" + CABECA_PWA, 1).encode("utf-8")


def ler_secrets():
    """Cidade e coordenadas do firmware, para quando o robo estiver desligado."""
    try:
        txt = SECRETS.read_text(encoding="utf-8")
    except OSError:
        return {}
    d = dict(re.findall(r'^\s*#define\s+(\w+)\s+"([^"]*)"', txt, re.M))
    return {"city": d.get("CIDADE", ""), "lat": d.get("LAT", ""), "lon": d.get("LON", "")}


def robo_get(robo_url, caminho, timeout=4):
    """Repassa um GET ao ESP32 (so as rotas do rosto). Devolve (codigo, corpo, tipo)."""
    rota = caminho.split("?", 1)[0]
    if rota not in ROBO_ROTAS:
        return 404, b'{"erro":"rota do robo desconhecida"}', "application/json"
    try:
        with urllib.request.urlopen(robo_url.rstrip("/") + caminho, timeout=timeout) as r:
            return r.status, r.read(), r.headers.get("Content-Type", "application/octet-stream")
    except urllib.error.HTTPError as e:
        return e.code, e.read(), "text/plain"
    except (urllib.error.URLError, OSError):
        return 502, '{"erro":"o robô não respondeu na rede de casa"}'.encode(), "application/json"


class CanalRobo:
    """O robo liga para ca pela internet e busca os comandos (long-poll em /robo-canal).

    Assim ele funciona em qualquer rede com internet: a ponte nao precisa alcancar o
    robo (roteador com isolamento de clientes, 5 GHz x 2,4 GHz, outra casa...).
    Um pedido do robo fica esperando ate ESPERA_S; chegou comando, ele volta na hora.
    """
    ESPERA_S = 25
    FILA_MAX = 40
    ONLINE_S = 8               # sem pedido esperando: online se o ultimo foi ha pouco

    def __init__(self):
        self.cond = threading.Condition()
        self.fila = []
        self.estado = {"mood": "neutro", "board": 0}
        self.visto = 0.0
        self.esperando = 0
        self.vez = 0           # cada pedido novo encerra o anterior (robo reiniciou, conexao velha)

    @property
    def online(self):
        with self.cond:
            return self.esperando > 0 or time.time() - self.visto < self.ONLINE_S

    def enviar(self, caminho):
        with self.cond:
            self.fila.append(caminho)
            del self.fila[:-self.FILA_MAX]
            self.cond.notify_all()

    def buscar(self, estado, espera=None):
        """Chamado pelo robo: guarda o estado dele e devolve os comandos (espera se nao houver)."""
        espera = self.ESPERA_S if espera is None else espera
        with self.cond:
            self.vez += 1
            minha = self.vez
            self.cond.notify_all()
            self.estado.update({k: v for k, v in estado.items() if v not in (None, "")})
            self.visto = time.time()
            self.esperando += 1
            try:
                fim = time.time() + espera
                while not self.fila and self.vez == minha:
                    falta = fim - time.time()
                    if falta <= 0:
                        break
                    self.cond.wait(falta)
                if self.vez != minha:
                    return []
                cmds, self.fila = self.fila, []
                return cmds
            finally:
                self.esperando -= 1
                self.visto = time.time()

    def estado_dict(self):
        with self.cond:
            return dict(self.estado)

    def estado_json(self):
        with self.cond:
            e = dict(self.estado)
        try:
            e["board"] = int(e.get("board", 0))
        except (TypeError, ValueError):
            e["board"] = 0
        return json.dumps({"mood": e.get("mood", "neutro"), "board": e["board"]}).encode()

    def info(self):
        with self.cond:
            return {k: self.estado.get(k, "") for k in ("ip", "rede")}


def robo_cmd(canal, robo_url, caminho):
    """Comando do rosto vindo do app: pelo canal da internet se o robo estiver ligado nele,
    senao direto na rede de casa. Devolve (codigo, corpo, tipo)."""
    rota = caminho.split("?", 1)[0]
    if rota not in ROBO_ROTAS:
        return 404, b'{"erro":"rota do robo desconhecida"}', "application/json"
    if canal and canal.online and rota != "/cfg":
        if rota == "/state":
            return 200, canal.estado_json(), "application/json"
        canal.enviar(caminho)
        if rota == "/mood":
            return 200, canal.estado_json(), "application/json"
        return 200, b"ok", "text/plain"
    return robo_get(robo_url, caminho)


class LigacaoUSB:
    """Robo no cabo USB do notebook: sem WiFi (so 5 GHz por perto, portal de evento),
    a ponte entrega os comandos do rosto pela serial.

    O robo imprime "@ZEUS mood=.. board=.. net=.. ip=.. rede=.." a cada segundo. Com
    net=1 o canal da internet ja funciona e o cabo so escuta; com net=0 a ponte busca
    os comandos no mesmo CanalRobo que o WiFi usaria e escreve uma linha por comando.
    """
    VIDS = {0x1A86, 0x303A, 0x10C4}   # CH340, USB da Espressif, CP210x
    MUDO_S = 3                         # sem "@ZEUS" ha mais que isso: cabo solto ou robo reiniciando
    AVISO_S = 2                        # sem comando: "/usb" para o robo saber que o cabo segue ligado
    ESPERA_S = 0.5

    def __init__(self, canal, porta="auto", abrir=None, achar=None):
        self.canal = canal
        self.porta = porta
        self._abrir = abrir or self._abrir_serial
        self._achar = achar or self.achar_porta
        self.ser = None
        self.estado = {}
        self.visto = 0.0
        self._buf = b""
        self._aviso = 0.0
        self.solta_ate = 0.0               # gravando o firmware: a porta fica livre ate aqui

    def soltar(self, segundos):
        """Larga a COM do robo por uns segundos (gravar o firmware) sem parar a ponte; 0 = pega de volta."""
        self.solta_ate = time.time() + max(0.0, min(float(segundos), 600.0)) if segundos else 0.0

    @property
    def solta(self):
        return time.time() < self.solta_ate

    @classmethod
    def achar_porta(cls):
        from serial.tools import list_ports
        for p in list_ports.comports():
            if p.vid in cls.VIDS:
                return p.device
        return None

    @staticmethod
    def _abrir_serial(porta):
        import serial
        s = serial.Serial()
        s.port, s.baudrate, s.timeout, s.write_timeout = porta, 115200, 0, 1
        s.dtr = s.rts = False          # sem isto abrir a porta reinicia o ESP32
        s.open()
        return s

    @property
    def ativo(self):
        """Robo no cabo e sem o canal da internet: os comandos vao pela serial."""
        return self.ser is not None and time.time() - self.visto < self.MUDO_S and self.estado.get("net") == "0"

    def ler(self):
        """Le o que chegou e atualiza o estado. False = a porta caiu."""
        try:
            self._buf += self.ser.read(4096)
        except Exception:
            return False
        *linhas, resto = self._buf.split(b"\n")
        self._buf = resto[-4096:]
        for linha in linhas:
            texto = linha.decode("utf-8", "replace")
            i = texto.find("@ZEUS ")
            if i < 0:
                continue                   # log do ESP32
            campos = dict(p.split("=", 1) for p in texto[i + 6:].split() if "=" in p)
            campos["rede"] = urllib.parse.unquote(campos.get("rede", ""))
            self.estado, self.visto = campos, time.time()
        return True

    def fechar(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser, self.estado, self._buf = None, {}, b""

    def passo(self):
        """Uma volta. Devolve quantos segundos dormir antes da proxima."""
        if self.solta:
            if self.ser is not None:
                self.fechar()
            return 0.2
        if self.ser is None:
            porta = self._achar() if self.porta == "auto" else self.porta
            if not porta:
                return 5
            try:
                self.ser = self._abrir(porta)
            except Exception:
                return 5                   # porta ocupada (idf.py flash/monitor) ou sumiu
        if not self.ler():
            self.fechar()
            return 2
        if not self.ativo:
            return 0.2
        est = {"mood": self.estado.get("mood"), "board": self.estado.get("board"), "ip": "", "rede": "cabo USB"}
        est.update({k: self.estado.get(k) for k in SNN_CAMPOS})
        cmds = self.canal.buscar(est, espera=self.ESPERA_S)
        agora = time.time()
        if not cmds:
            if agora - self._aviso < self.AVISO_S:
                return 0
            cmds = ["/usb"]
        try:
            self.ser.write(("\n".join(cmds) + "\n").encode("utf-8"))
        except Exception:
            self.fechar()
            return 2
        self._aviso = agora
        return 0

    def rodar(self):
        while True:
            espera = self.passo()
            if espera:
                time.sleep(espera)


def cfg_app(robo_url, canal=None):
    """Configuracao do app pela internet: a do robo (cidade etc.), sem o endereco da ponte de casa."""
    pelo_canal = bool(canal and canal.online)
    code, corpo = 0, b""
    if not pelo_canal:
        code, corpo, _ = robo_get(robo_url, "/cfg")
    cfg = {}
    if code == 200:
        try:
            cfg = json.loads(corpo)
        except ValueError:
            cfg = {}
    for k, v in ler_secrets().items():
        cfg.setdefault(k, v)
    cfg["ponte"] = ""                      # a pagina usa o proprio endereco (https)
    cfg["robo_online"] = pelo_canal or code == 200
    if pelo_canal:
        cfg.update({k: v for k, v in canal.info().items() if v})
    return cfg


def arquivo_aplicativo(nome):
    """Arquivos publicos de /aplicativos e icones. Devolve (bytes, tipo) ou None."""
    tipos = {".png": "image/png", ".apk": "application/vnd.android.package-archive", ".svg": "image/svg+xml"}
    p = (APLICATIVOS / nome).resolve()
    if p.parent != APLICATIVOS.resolve() or not p.is_file() or p.suffix not in tipos:
        return None
    return p.read_bytes(), tipos[p.suffix]


def pagina_aplicativos(host):
    apk = APLICATIVOS / "zeus.apk"
    info = ""
    if apk.exists():
        mb = apk.stat().st_size / 1e6
        quando = time.strftime("%d/%m/%Y %H:%M", time.localtime(apk.stat().st_mtime))
        info = f"{mb:.2f} MB · atualizado em {quando}"
    url = f"https://{host}/"
    return f"""<!doctype html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Zeus - aplicativos</title>
<link rel="icon" href="/icon-192.png"><link rel="apple-touch-icon" href="/apple-touch-icon.png">
<style>
:root{{--bg:#0b0b0b;--card:#1b1b1b;--txt:#eee;--dim:#9a9a9a;--azul:#5aa9ff;--verde:#22c55e}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--txt);font:16px/1.5 system-ui}}
main{{max-width:680px;margin:0 auto;padding:28px 16px 48px}}
header{{display:flex;gap:14px;align-items:center}}header img{{width:64px;height:64px;border-radius:16px}}
h1{{margin:0;font-size:28px;letter-spacing:2px}}header p{{margin:2px 0 0;color:var(--dim)}}
section{{background:var(--card);border-radius:16px;padding:18px;margin-top:18px}}
h2{{margin:0 0 6px;font-size:20px}}.dim{{color:var(--dim);font-size:14px}}
ol{{padding-left:20px;margin:10px 0 0}}li{{margin:6px 0}}
a.botao{{display:block;text-align:center;margin-top:12px;padding:16px;border-radius:14px;background:var(--verde);
color:#000;font-weight:800;text-decoration:none;font-size:18px}}
a.botao.azul{{background:var(--azul)}}code{{background:#000;padding:2px 6px;border-radius:6px}}
</style></head><body><main>
<header><img src="/icon-192.png" alt=""><div><h1>ZEUS</h1><p>robô de bancada que conversa, controla o notebook e joga xadrez</p></div></header>

<section><h2>Android</h2>
<p class="dim">App com reconhecimento de voz e voz do próprio Android, câmera para fotos e modo Alexa ("Zeus, que horas são?"). {info}</p>
<a class="botao" href="/aplicativos/zeus.apk" download>Baixar o app para Android</a>
<ol>
<li>Abra o arquivo baixado. Se o Android pedir, permita <b>instalar apps desta fonte</b>.</li>
<li>O Play Protect pode avisar "app desconhecido": o app não está na Play Store. Toque em <b>Instalar mesmo assim</b>.</li>
<li>Abra o Zeus e digite o <b>código de acesso</b> uma vez.</li>
</ol></section>

<section><h2>iPhone</h2>
<p class="dim">No iPhone o Zeus funciona como app da tela de início (sem App Store): abre em tela cheia, com ícone próprio.</p>
<a class="botao azul" href="{url}">Abrir o Zeus no Safari</a>
<ol>
<li>Abra <code>{url}</code> no <b>Safari</b>.</li>
<li>Toque em <b>Compartilhar</b> (quadrado com seta) e depois em <b>Adicionar à Tela de Início</b>.</li>
<li>Abra o Zeus pelo ícone, digite o <b>código de acesso</b> e permita o microfone.</li>
</ol>
<p class="dim">No iPhone a foto pelo celular ainda não está disponível; voz, conversa, xadrez, webcam e o controle do notebook funcionam.</p></section>

<section><h2>Pelo navegador (qualquer aparelho)</h2>
<p class="dim">Também funciona direto no Chrome ou Safari, em <code>{url}</code>.</p></section>
</main></body></html>""".encode("utf-8")
