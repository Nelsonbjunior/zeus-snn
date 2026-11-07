#!/usr/bin/env python3
"""Ponte Zeus -> notebook Windows. So executa acoes de uma lista fixa; nunca roda comando livre.

Adaptada do mac/cozmo_mac.py do github.com/fabriciowth/cozmo-esp32 (MIT).
Diferencas principais: escuta na rede local (o pedido vem do celular Android, nao do
proprio PC), entao toda acao exige o token de zeus_ponte.json; e repassa a conversa
para o Ollama local (/llm), que so escuta em 127.0.0.1.

Uso:  py -3.13 zeus_ponte.py
"""
import json
import os
import queue
import re
import secrets
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, quote, quote_plus, unquote, urlparse

import camera as camera_mod
import cerebro_snn
import instagram as instagram_mod
import remoto
import voz as voz_mod
import xadrez

HERE = Path(__file__).resolve().parent
CONFIG = HERE / "zeus_ponte.json"
DEFAULT_PORT = 8770

APPS = {
    "whatsapp": ("uri", "whatsapp:", "WhatsApp"),
    "vscode": ("vscode", None, "VS Code"),
    "chrome": ("chrome", None, "Chrome"),
    "explorador": ("argv", ["explorer.exe"], "Explorador de Arquivos"),
    "calculadora": ("argv", ["calc.exe"], "Calculadora"),
    "bloco_de_notas": ("argv", ["notepad.exe"], "Bloco de Notas"),
    "configuracoes": ("uri", "ms-settings:", "Configurações do Windows"),
    "spotify": ("uri", "spotify:", "Spotify"),
}
SITES = {
    "agenda": "https://calendar.google.com",
    "gmail": "https://mail.google.com",
    "drive": "https://drive.google.com",
    "youtube": "https://www.youtube.com",
    "github": "https://github.com",
    "maps": "https://www.google.com/maps",
}
SEARCH = {
    "google": "https://www.google.com/search?q=",
    "youtube": "https://www.youtube.com/results?search_query=",
}
# Fechar: abas so do navegador e so quando a aba esta na frente (o titulo da janela e o
# da aba ativa); apps pelo "X" da janela, entao o app pode perguntar se quer salvar.
NAVEGADORES = ("firefox.exe", "chrome.exe", "msedge.exe", "brave.exe", "opera.exe")
ABAS = {
    "youtube": ("youtube",),
    "gmail": ("gmail",),
    "agenda": ("google agenda", "google calendar"),
    "drive": ("google drive", "meu drive", "my drive"),
    "github": ("github",),
    "maps": ("google maps",),
    "xadrez": ("zeus - xadrez",),
    "instagram": ("instagram",),          # "(1) Instagram - Google Chrome", "Criar novo post • Instagram"
}
APPS_FECHAR = {      # alvo: (executaveis, titulos exatos, nome falado). VS Code fica de fora.
    "whatsapp": (("whatsapp.exe", "whatsapp.root.exe"), ("whatsapp",), "WhatsApp"),
    "calculadora": (("calculatorapp.exe",), ("calculator", "calculadora"), "Calculadora"),
    "bloco_de_notas": (("notepad.exe",), (), "Bloco de Notas"),
    "spotify": (("spotify.exe",), (), "Spotify"),
    "chrome": (("chrome.exe",), (), "Chrome"),
    "firefox": (("firefox.exe",), (), "Firefox"),
    "explorador": (("explorer.exe",), (), "Explorador de Arquivos"),
    "configuracoes": ((), ("settings", "configurações"), "Configurações"),
}
CAMERA_URI = "microsoft.windows.camera:"
CAMERA_EXE = "WindowsCamera.exe"

# A pagina que o Zeus escreve: um arquivo so, sempre o mesmo
PAGE = HERE / "pagina" / "index.html"
MAX_HTML = 200_000
EMPTY = "<!doctype html>\n<html lang=\"pt-BR\">\n<head><meta charset=\"utf-8\"><title>Pagina</title></head>\n<body></body>\n</html>\n"
RELOAD = (b"<script>setInterval(async()=>{try{const v=await(await fetch('/pagina/v')).text();"
          b"if(window._v&&v!==window._v)location.reload();window._v=v}catch(e){}},700)</script>")

OLLAMA_URL = "http://127.0.0.1:11434"
OLLAMA_MODELO = "llama3.1:8b"      # precisa ter "tools" em `ollama show`
MAX_LLM_BODY = 1_500_000           # a conversa pode levar o HTML da pagina inteira
LLM_TIMEOUT_S = 90                 # resposta do Ollama (normal: 1-3 s)
LLM_FILA_S = 95                    # espera maxima pela vez de usar a GPU

PAINEL_XADREZ = HERE / "xadrez_painel.html"   # todas as partidas no navegador do notebook
PLACAR = HERE / "placar_xadrez.json"          # Zeus x cada aparelho (sobrevive a reinicios)
PENSAR_MIN_S = 1.5                            # o OLED mostra o Zeus "calculando" pelo menos isso
VOZ_ESPERA_S = 1.5                            # voz do notebook ocupada por mais que isso: fala no celular
PAINEL_ABERTO_S = 4                           # o painel consulta a cada 0,8 s: aberto se consultou ha pouco
PAINEL_TITULO = "Zeus - xadrez"               # <title> do painel = titulo da janela do Chrome (modo app)
FOTOS = HERE / "fotos"            # fotos que o celular manda (para postar no Instagram)
MAX_FOTO = 15_000_000
MEMORIA = HERE / "cerebro"         # copia de seguranca do cerebro (SNN) do robo, com uma copia por dia
MAX_MEMORIA = 64_000
MEMORIA_DIAS = 60                  # copias diarias guardadas
INSTA_TTL = 300                   # rascunho do Instagram vale 5 min
INSTA_MAX = 2200                  # limite de legenda do Instagram

WA_MAX = 1000
WA_TTL = 120                      # rascunho vale 2 min


def load_config(path=CONFIG):
    """Le zeus_ponte.json; na primeira vez cria com um token aleatorio."""
    if path.exists():
        cfg = json.loads(path.read_text(encoding="utf-8"))
    else:
        cfg = {}
    changed = False
    if not cfg.get("token"):
        cfg["token"] = secrets.token_urlsafe(18)
        changed = True
    if not cfg.get("porta"):
        cfg["porta"] = DEFAULT_PORT
        changed = True
    if not cfg.get("hotspot_senha"):              # rede do Hotspot Movel do PC (secrets.h WIFI_PASS)
        cfg["hotspot_senha"] = secrets.token_urlsafe(9)
        changed = True
    if not cfg.get("codigo_acesso"):              # para entrar pela internet (zeus.appdevtest.shop)
        cfg["codigo_acesso"] = "".join(str(secrets.randbelow(10)) for _ in range(8))
        changed = True
    for k, v in (("stockfish", ""), ("ollama", OLLAMA_URL), ("modelo", OLLAMA_MODELO),
                 ("robo", "http://192.168.137.201"), ("usb", "auto"), ("hotspot", "ZEUS-PC")):
        if k not in cfg:
            cfg[k] = v
            changed = True
    if changed:
        path.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding="utf-8")
    return cfg


def lan_ip():
    """IP do notebook na rede local (o que o celular precisa usar)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))      # nao envia nada: so escolhe a interface
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def private_origin(origin):
    """Aceita CORS so de paginas na rede local (o Zeus) ou do proprio PC."""
    if not origin:
        return False
    u = urlparse(origin)
    host = u.hostname or ""
    if u.scheme != "http" or not host:
        return False
    if host in ("localhost", "127.0.0.1"):
        return True
    parts = host.split(".")
    if len(parts) != 4 or not all(p.isdigit() for p in parts):
        return False
    a, b = int(parts[0]), int(parts[1])
    return a == 10 or (a == 192 and b == 168) or (a == 172 and 16 <= b <= 31)


def find_exe(*cands):
    for c in cands:
        if c and Path(c).is_file():
            return str(c)
    return None


def chrome_path():
    return find_exe(*[Path(os.environ.get(v, "")) / "Google/Chrome/Application/chrome.exe"
                      for v in ("PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA") if os.environ.get(v)])


def vscode_path():
    return find_exe(Path(os.environ.get("LOCALAPPDATA", "")) / "Programs/Microsoft VS Code/Code.exe",
                    Path(os.environ.get("PROGRAMFILES", "")) / "Microsoft VS Code/Code.exe")


class Ponte:
    """Estado e acoes. `io` e o modulo windows_io (ou um falso nos testes)."""

    def __init__(self, io, token, port=DEFAULT_PORT, partida=None, page=PAGE,
                 ollama=OLLAMA_URL, modelo=OLLAMA_MODELO, camera=None, insta=None, fotos=FOTOS,
                 codigo=None, robo="http://192.168.0.201", voz=None, placar=None, memoria=MEMORIA):
        self.io = io
        self.acesso = remoto.Acesso(codigo) if codigo else None
        self.robo_url = robo
        self.canal = remoto.CanalRobo()         # o robo busca os comandos pela internet
        self._rosto = queue.Queue(maxsize=200)  # comandos do rosto vindos da propria ponte (boca da voz)
        threading.Thread(target=self._rosto_worker, daemon=True).start()
        self.voz = voz or voz_mod.Voz(boca=self.rosto)
        self.camera = camera or camera_mod.Camera()
        self._insta = insta
        self.fotos = Path(fotos)
        self.memoria = Path(memoria)
        self.vocabulario = cerebro_snn.Vocabulario(self.memoria / "palavroes.json")
        self.usb = None                         # remoto.LigacaoUSB quando o robo esta no cabo
        # ao reiniciar a ponte, a ultima foto continua sendo a mais nova da pasta
        antigas = sorted(self.fotos.glob("foto_*.jpg")) if self.fotos.exists() else []
        self.ultima_foto = antigas[-1] if antigas else None
        self.insta_draft = {"id": None, "at": 0.0}
        self.token = token
        self.port = port
        self.page = page
        self.version = 0          # muda a cada gravacao; o Chrome recarrega quando muda
        self.partida = partida or xadrez.Partida()
        self.partidas = xadrez.Partidas(self.partida, placar)   # uma partida por aparelho do app
        self.painel_visto = 0.0                 # ultima consulta do painel do xadrez aberto no notebook
        self.painel_foco = {"id": "", "seq": 0}  # o painel rola ate a partida de quem pediu
        self.draft = {"id": None, "at": 0.0}
        self.lock = threading.Lock()
        self.ollama = ollama.rstrip("/")
        self.modelo = modelo
        self.llm_lock = threading.Lock()        # uma GPU: um pedido por vez
        self.llm_ligado = True                  # False: so a rede neural (SNN) reage, sem o LLM
        self.arq_llm = None                     # arquivo que guarda "LLM desligado" entre reinicios

    def rosto(self, caminho):
        """Manda um comando ao rosto sem esperar (a voz chama isto a cada palavra)."""
        try:
            self._rosto.put_nowait(caminho)
        except queue.Full:
            pass

    def _rosto_worker(self):
        while True:
            caminho = self._rosto.get()
            if self.canal.online:
                self.canal.enviar(caminho)
            elif self._rosto.qsize() < 3:              # pela rede de casa so se nao estiver atrasado
                remoto.robo_get(self.robo_url, caminho, timeout=1)

    def xadrez(self, partida, dispositivo, acao, **kw):
        """nova/jogar/encerrar. Enquanto o Zeus escolhe o lance, o OLED mostra ele calculando
        (com o nome do aparelho); depois mostra o lance grande. O tabuleiro nao vai ao OLED."""
        quem = self.partidas.nome(dispositivo) if dispositivo else ""
        partida.ao_pensar = lambda on: self.rosto("/think?on=1&n=" + quote(quem) if on else "/think?on=0")
        try:
            s = getattr(partida, acao)(**kw)
        finally:
            partida.ao_pensar = None
        if acao == "encerrar":
            return s
        if self.partidas.registrar(dispositivo, partida):
            print(f"-> xadrez: fim contra {quem}: {s['resultado']}")
        if s.get("fim"):                          # Zeus ganhou: feliz; perdeu: triste
            self.rosto("/show?t=" + quote(s["placa"]) + "&m=" + {"MATE!": "1", "VOCE!": "2"}.get(s["placa"], "0"))
        elif s.get("zeus"):
            self.rosto("/show?t=" + quote(s["zeus"]["san_pt"]) + "&m=1")
        return s

    def abrir_painel(self, dispositivo):
        """'ver no notebook': com o painel ja aberto, so traz a janela para a frente e rola ate a
        partida do aparelho; senao abre o painel numa janela propria do Chrome (modo app, sem
        abas). Antes cada pedido abria um Chrome novo, e com varias partidas as janelas se
        acumulavam e cada uma demorava a abrir."""
        self.painel_foco = {"id": (dispositivo or "")[:16], "seq": self.painel_foco["seq"] + 1}
        if time.time() - self.painel_visto < PAINEL_ABERTO_S and hasattr(self.io, "windows"):
            for hwnd, titulo, exe in self.io.windows():
                if titulo.startswith(PAINEL_TITULO) and (exe or "").lower() == "chrome.exe":
                    self.io.focus(hwnd)
                    return "painel do xadrez na frente"
        url = self.chess_url + (f"?d={dispositivo}" if dispositivo else "")
        exe = chrome_path()
        if exe:
            self.io.spawn([exe, "--app=" + url])
        else:
            self.io.open_uri(url)                 # sem Chrome: navegador padrao
        return "painel do xadrez no Chrome"

    @property
    def page_url(self):
        return f"http://127.0.0.1:{self.port}/pagina"

    @property
    def chess_url(self):
        return f"http://127.0.0.1:{self.port}/xadrez"

    # ---- abrir coisas: so alvos das listas fixas ----
    def _launch(self, kind, value, extra=None):
        if kind == "uri":
            self.io.open_uri(value)
        elif kind == "argv":
            self.io.spawn(value)
        elif kind == "chrome":
            exe = chrome_path()
            if exe:
                self.io.spawn([exe] + ([extra] if extra else []))
            elif extra:
                self.io.open_uri(extra)           # sem Chrome: navegador padrao
            else:
                raise RuntimeError("não achei o Chrome instalado neste notebook")
        elif kind == "vscode":
            exe = vscode_path()
            if not exe:
                raise RuntimeError("não achei o VS Code instalado neste notebook")
            self.io.spawn([exe] + ([extra] if extra else []))

    @property
    def insta(self):
        if self._insta is None:                   # posta pelo Chrome normal, via extensao
            self._insta = instagram_mod.Instagram(abrir_url=lambda u: self._launch("chrome", None, u))
        return self._insta

    def open_target(self, t):
        if t == "instagram":                      # no Chrome normal, onde a extensao do Zeus posta
            r = self.insta.abrir()
            if not r["extensao"]:
                return ("Instagram aberto no Chrome, mas a extensão do Zeus não respondeu: instale a "
                        "extensão da pasta ponte\\extensao_instagram para eu poder postar")
            if not r["logado"]:
                return "Instagram aberto no Chrome; entre na sua conta para eu poder postar"
            return "Instagram aberto no Chrome"
        if t in APPS:
            kind, value, name = APPS[t]
            self._launch(kind, value)
            return f"app {name}"
        if t in SITES:
            self.io.open_uri(SITES[t])
            return f"site {t}"
        raise ValueError(f"alvo desconhecido: {t}")

    def search(self, site, q):
        q = (q or "").strip()
        if site not in SEARCH:
            raise ValueError(f"busca desconhecida: {site}")
        if not q or len(q) > 200:
            raise ValueError("busca vazia ou longa demais")
        self.io.open_uri(SEARCH[site] + quote_plus(q))
        return f"busca {site}: {q}"

    def camera_aberta(self):
        code, out = self.io.run(["tasklist", "/FI", f"IMAGENAME eq {CAMERA_EXE}", "/NH"])
        return CAMERA_EXE.lower() in out.lower()

    def webcam(self, acao):
        """A imagem da webcam vai para o app (video /webcam/stream), nao para o app Camera."""
        if acao == "alternar":                      # (app antigo) faz o contrario do que esta
            acao = "desligar" if self.camera.ativa else "ligar"
        if acao == "ligar":
            if self.camera.ativa:                   # outro aparelho ja ligou: so entra no video
                return "câmera ligada: a imagem aparece no app"
            # a webcam so atende um programa por vez: nao fecha o app dos outros, avisa
            if self.camera_aberta():
                raise RuntimeError("o app Câmera do Windows está usando a webcam do notebook; "
                                   "feche-o para eu mostrar a câmera")
            self.camera.ligar()
            return "câmera ligada: a imagem aparece no app"
        if acao == "sair":                          # um aparelho fechou o video; os outros continuam
            time.sleep(0.6)                         # a conexao do video dele termina
            if self.camera.ativa and getattr(self.camera, "publico", 0) == 0:
                self.camera.desligar()
                return "câmera desligada"
            if self.camera.ativa:
                return "você saiu do vídeo; a câmera continua ligada em outro aparelho"
            return "câmera desligada"
        if acao == "desligar":                      # pedido explicito: desliga para todos
            estava = self.camera.ativa
            self.camera.desligar()
            return "câmera desligada" if estava else "a câmera já estava desligada"
        raise ValueError(f"ação da webcam desconhecida: {acao}")

    def close_target(self, t):
        if t == "webcam":
            return self.webcam("desligar")
        if t == "instagram":                      # fechar a aba descarta o post que estava esperando
            self.insta_draft["id"] = None
            if self._insta is not None:
                self._insta.rascunho_pronto = False
        if t in ABAS:
            chaves = ABAS[t]
            alvos = [h for h, tit, exe in self.io.windows()
                     if exe.lower() in NAVEGADORES and any(k in tit.lower() for k in chaves)]
            if not alvos:
                raise ValueError(f"não achei a aba do {t} na frente de nenhuma janela do navegador")
            fechadas = 0
            for h in alvos:
                self.io.focus(h)
                titulo, exe = self.io.foreground()
                # so aperta Ctrl+W se a janela da frente for mesmo o navegador com essa aba
                if self.io.foreground_hwnd() != h or not any(k in titulo.lower() for k in chaves):
                    continue
                self.io.keys("ctrl", "w")
                fechadas += 1
            if not fechadas:
                raise RuntimeError("o Windows não deixou trazer o navegador para a frente; não fechei nada")
            return f"aba do {t} fechada" + (f" em {fechadas} janelas" if fechadas > 1 else "")
        if t in APPS_FECHAR:
            exes, titulos, nome = APPS_FECHAR[t]
            alvos = [h for h, tit, exe in self.io.windows()
                     if (exe.lower() in exes or tit.strip().lower() in titulos)
                     and tit.strip().lower() != "program manager"]       # a area de trabalho
            if not alvos:
                return f"{nome} já estava fechado"
            for h in alvos:
                self.io.close_window(h)
            return f"{nome} fechado"
        raise ValueError(f"não sei fechar: {t}")

    # ---- pagina web criada por voz ----
    def page_write(self, html):
        if not isinstance(html, str) or not html.strip():
            raise ValueError("html vazio")
        if len(html) > MAX_HTML:
            raise ValueError("html grande demais")
        self.page.parent.mkdir(exist_ok=True)
        self.page.write_text(html, encoding="utf-8")
        with self.lock:
            self.version += 1

    def page_read(self):
        return self.page.read_text(encoding="utf-8") if self.page.exists() else EMPTY

    def page_open(self, where):
        if not self.page.exists():
            self.page_write(EMPTY)
        if where == "vscode":
            self._launch("vscode", None, str(self.page))
            return "página no VS Code"
        if where == "chrome":
            self._launch("chrome", None, self.page_url)
            return "página no Chrome"
        raise ValueError(f"onde abrir? {where}")

    # ---- cerebro: Ollama local ----
    def memoria_guardar(self, dados):
        """O robo mandou a memoria do cerebro: guarda a atual e uma copia do dia."""
        if not dados or len(dados) > MAX_MEMORIA or dados[:4] != b"ZSN1":
            raise ValueError("memoria do cerebro invalida")
        self.memoria.mkdir(parents=True, exist_ok=True)
        tmp = self.memoria / "memoria.tmp"
        tmp.write_bytes(dados)
        os.replace(tmp, self.memoria / "memoria.bin")
        (self.memoria / time.strftime("memoria_%Y-%m-%d.bin")).write_bytes(dados)
        for velha in sorted(self.memoria.glob("memoria_*.bin"))[:-MEMORIA_DIAS]:
            velha.unlink()
        return len(dados)

    def memoria_ler(self):
        arq = self.memoria / "memoria.bin"
        return arq.read_bytes() if arq.exists() else None

    def snn(self, estimulos):
        """Manda estimulos para a rede neural do robo: [(nome, 0..100)]."""
        for nome, v in estimulos:
            self.rosto(f"/snn?s={nome}&v={int(v)}")

    def llm(self, messages, tools=None):
        """Um passo da conversa. Quem executa as funcoes pedidas e a pagina do celular.
        Em paralelo, a rede neural do robo sente a fala e o LLM pensando, e o LLM recebe
        os sentimentos da rede (cerebro_snn)."""
        if not isinstance(messages, list) or not messages:
            raise ValueError("conversa vazia")
        ultima = messages[-1] if isinstance(messages[-1], dict) else {}
        est = []
        if ultima.get("role") == "user":                 # fala nova (nao o retorno de uma funcao)
            est = cerebro_snn.estimulos_da_fala(ultima.get("content"))
            self.snn(est)
            for p in self.vocabulario.ouvir(ultima.get("content")):
                print(f"-> o Zeus aprendeu o palavrao: {p}")
        aprendidos = self.vocabulario.aprendidos()
        if not self.llm_ligado:
            # LLM desligado: a rede aprende sozinha (sem licao nem dopamina do LLM) e o Zeus so faz um som
            som = cerebro_snn.som_do_humor(self.canal.estado_dict(), est, aprendidos)
            return {"ok": True, "message": {"role": "assistant", "content": som}, "segundos": 0.0, "llm": False}
        messages = cerebro_snn.com_sentimentos(messages, self.canal.estado_dict(), aprendidos,
                                               dono_xingou=("bronca", 100) in est)
        self.rosto("/snn?s=trabalho&c=100")             # a rede sente o LLM pensando
        try:
            r = self._llm(messages, tools)
        finally:
            self.rosto("/snn?s=trabalho&c=0")
        calls = [c.get("function", {}).get("name") for c in r["message"].get("tool_calls") or []]
        self.snn(cerebro_snn.estimulos_das_funcoes(calls))
        return r

    def _llm(self, messages, tools=None):
        body = {"model": self.modelo, "messages": messages, "tools": tools or [], "stream": False,
                "keep_alive": "30m", "options": {"temperature": 0.4, "num_ctx": 8192}}
        req = urllib.request.Request(self.ollama + "/api/chat", data=json.dumps(body).encode(),
                                     headers={"Content-Type": "application/json"})
        t = time.time()
        # fila com prazo: um pedido preso no Ollama nao segura os outros para sempre
        if not self.llm_lock.acquire(timeout=LLM_FILA_S):
            raise RuntimeError("o cérebro está ocupado com outro pedido; tente de novo")
        try:
            with urllib.request.urlopen(req, timeout=LLM_TIMEOUT_S) as r:
                d = json.load(r)
        except urllib.error.HTTPError as e:
            raise RuntimeError(f"o Ollama recusou: {e.read().decode(errors='replace')[:300]}")
        except TimeoutError as e:
            raise RuntimeError("o cérebro demorou demais para responder") from e
        except (urllib.error.URLError, OSError) as e:
            if isinstance(getattr(e, "reason", None), TimeoutError):
                raise RuntimeError("o cérebro demorou demais para responder") from e
            raise RuntimeError(f"o Ollama não respondeu no notebook ({self.ollama}): abra o app Ollama") from e
        finally:
            self.llm_lock.release()
        return {"ok": True, "message": d.get("message") or {}, "segundos": round(time.time() - t, 1)}

    def aquecer(self):
        """Carrega o modelo na GPU ao iniciar, para a primeira resposta nao demorar."""
        try:
            self.llm([{"role": "user", "content": "oi"}])
            print(f"cerebro: {self.modelo} carregado")
        except RuntimeError as e:
            print(f"cerebro: {e}")

    # ---- foto do celular e Instagram ----
    def foto_salvar(self, dados):
        if not dados or len(dados) > MAX_FOTO or not dados.startswith(b"\xff\xd8"):
            raise ValueError("a foto precisa ser um JPEG de até 15 MB")
        self.fotos.mkdir(exist_ok=True)
        nome = time.strftime("foto_%Y%m%d_%H%M%S.jpg")
        caminho = self.fotos / nome
        caminho.write_bytes(dados)
        self.ultima_foto = caminho
        return nome

    def insta_rascunho(self, legenda):
        legenda = (legenda or "").strip()
        if len(legenda) > INSTA_MAX:
            raise ValueError("legenda longa demais para o Instagram")
        if not self.ultima_foto or not self.ultima_foto.exists():
            raise ValueError("não tem foto: peça para tirar uma foto com o celular antes")
        self.insta.rascunho(self.ultima_foto, legenda)
        self.insta_draft = {"id": secrets.token_hex(4), "at": time.time()}
        return self.insta_draft["id"]

    def insta_publicar(self, draft_id):
        if not self.insta_draft["id"] or draft_id != self.insta_draft["id"]:
            raise ValueError("não há publicação pronta com esse id: prepare o rascunho antes")
        if time.time() - self.insta_draft["at"] > INSTA_TTL:
            self.insta_draft["id"] = None
            raise ValueError("o rascunho expirou: prepare de novo")
        self.insta_draft["id"] = None              # um rascunho publica uma vez so
        r = self.insta.publicar()
        self.foto_limpar()                         # publicada: nao pode ser postada de novo
        return r

    def foto_limpar(self):
        """Depois de publicar, apaga as fotos do notebook: a proxima postagem exige foto nova
        (sem isso, ao reiniciar a ponte, uma foto antiga voltaria a ser a "ultima")."""
        self.ultima_foto = None
        apagadas = 0
        for f in self.fotos.glob("foto_*.jpg") if self.fotos.exists() else []:
            try:
                f.unlink()
                apagadas += 1
            except OSError:
                pass
        return apagadas

    def insta_cancelar(self):
        self.insta_draft["id"] = None
        return self.insta.cancelar()

    # ---- WhatsApp: escreve no chat aberto. Envio so com rascunho valido. ----
    def _whatsapp_na_frente(self):
        self.io.open_uri("whatsapp:")
        for _ in range(30):
            title, exe = self.io.foreground()
            if "whatsapp" in exe.lower() or title.strip().lower() == "whatsapp":
                return
            time.sleep(0.1)
        self.io.bring_to_front("WhatsApp")
        time.sleep(0.3)
        title, exe = self.io.foreground()
        if "whatsapp" in exe.lower() or title.strip().lower() == "whatsapp":
            return
        # nunca digita em outra janela
        raise RuntimeError("o WhatsApp não ficou na frente no notebook: abra o WhatsApp, "
                           "entre na conversa e tente de novo")

    def wa_draft(self, text):
        if not isinstance(text, str) or not text.strip():
            raise ValueError("mensagem vazia")
        if len(text) > WA_MAX:
            raise ValueError("mensagem longa demais")
        self._whatsapp_na_frente()
        # o texto vai pela area de transferencia (aceita acento) e volta o que voce tinha copiado
        old = self.io.clip_get()
        self.io.clip_set(text)
        try:
            time.sleep(0.15)
            self.io.keys("ctrl", "v")
        finally:
            time.sleep(0.3)
            self.io.clip_set(old or "")
        self.draft["id"], self.draft["at"] = secrets.token_hex(4), time.time()
        return self.draft["id"]

    def wa_send(self, draft_id):
        if not self.draft["id"] or draft_id != self.draft["id"]:
            raise ValueError("não há rascunho com esse id: escreva o rascunho antes")
        if time.time() - self.draft["at"] > WA_TTL:
            self.draft["id"] = None
            raise ValueError("rascunho expirou: escreva de novo")
        self.draft["id"] = None            # um rascunho envia uma vez so
        self._whatsapp_na_frente()
        self.io.keys("enter")

    def wa_cancel(self):
        if not self.draft["id"]:                   # sem rascunho nosso: nao apaga o que a pessoa digitou
            return False
        self.draft["id"] = None
        self._whatsapp_na_frente()
        self.io.keys("ctrl", "a")
        self.io.keys("backspace")


# ---------------- HTTP ----------------



def make_handler(ponte):
    class Handler(BaseHTTPRequestHandler):
        # HTTP/1.0: cada pedido fecha a conexao (um 403 sem ler o corpo nao suja o proximo)

        def cors(self):
            origin = self.headers.get("Origin")
            if private_origin(origin):
                self.send_header("Access-Control-Allow-Origin", origin)
                self.send_header("Vary", "Origin")
            # Chrome pede isso quando uma pagina da rede chama o localhost
            self.send_header("Access-Control-Allow-Private-Network", "true")
            self.send_header("Access-Control-Allow-Methods", "GET, POST")
            self.send_header("Access-Control-Allow-Headers", "Content-Type, X-Zeus-Token, X-Zeus-Dispositivo, X-Zeus-Nome")

        def reply(self, code, body):
            data = json.dumps(body, ensure_ascii=False).encode()
            self.send_response(code)
            self.cors()
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def raw(self, code, data, ctype):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def authorized(self):
            tok = self.headers.get("X-Zeus-Token") or ""
            return secrets.compare_digest(tok.encode(), ponte.token.encode())

        def do_OPTIONS(self):
            self.send_response(204)
            self.cors()
            self.send_header("Content-Length", "0")
            self.end_headers()

        def publico(self):
            """Pedido que veio pela internet (o Cloudflare marca o IP de origem)."""
            return bool(self.headers.get("Cf-Connecting-Ip"))

        def dispositivo(self):
            """Codigo aleatorio do aparelho (app) ou ?d= (tabuleiro do notebook): separa as partidas."""
            d = self.headers.get("X-Zeus-Dispositivo") or parse_qs(urlparse(self.path).query).get("d", [""])[0]
            return d if re.fullmatch(r"[A-Za-z0-9-]{8,64}", d or "") else ""

        def nome_aparelho(self):
            return unquote(self.headers.get("X-Zeus-Nome") or "")

        def partida(self, marcar=True):
            if not marcar:                            # painel do notebook: nao conta como aparelho ligado
                return ponte.partidas.todas.get(self.dispositivo()) or ponte.partidas.padrao
            return ponte.partidas.de(self.dispositivo(), self.nome_aparelho())

        def ip(self):
            return self.headers.get("Cf-Connecting-Ip") or self.client_address[0]

        def do_GET(self):
            u = urlparse(self.path)
            qs = parse_qs(u.query)
            arg = lambda k: qs.get(k, [""])[0]
            # ---- app e downloads (abertos; as acoes continuam pedindo o token) ----
            if u.path in ("/", "/index.html"):
                return self.raw(200, remoto.pagina_app(), "text/html; charset=utf-8")
            if u.path == "/manifest.json":
                return self.raw(200, json.dumps(remoto.MANIFEST).encode(), "application/manifest+json")
            if u.path in ("/aplicativos", "/aplicativos/"):
                host = self.headers.get("Host") or "zeus.appdevtest.shop"
                return self.raw(200, remoto.pagina_aplicativos(host), "text/html; charset=utf-8")
            if u.path.startswith("/aplicativos/") or u.path in ("/icon-192.png", "/icon-512.png",
                                                               "/apple-touch-icon.png", "/icon.svg"):
                arq = remoto.arquivo_aplicativo(u.path.rsplit("/", 1)[1])
                if not arq:
                    return self.reply(404, {"erro": "arquivo não encontrado"})
                self.send_response(200)
                self.send_header("Content-Type", arq[1])
                self.send_header("Content-Length", str(len(arq[0])))
                if arq[1].endswith("archive"):
                    self.send_header("Content-Disposition", 'attachment; filename="Zeus.apk"')
                self.end_headers()
                return self.wfile.write(arq[0])
            # paginas so para o Chrome do proprio notebook: nao aparecem na internet
            if self.publico() and (u.path.startswith("/pagina") or u.path.startswith("/xadrez")):
                return self.reply(404, {"erro": "rota desconhecida"})
            # previews (so leitura): abertos no Chrome do proprio notebook
            if u.path == "/pagina":
                data = ponte.page_read().encode()
                data = data.replace(b"</body>", RELOAD + b"</body>") if b"</body>" in data else data + RELOAD
                return self.raw(200, data, "text/html; charset=utf-8")
            if u.path == "/pagina/v":
                return self.raw(200, str(ponte.version).encode(), "text/plain")
            if u.path == "/xadrez":                   # painel: todas as partidas e o placar
                return self.raw(200, PAINEL_XADREZ.read_bytes(), "text/html; charset=utf-8")
            if u.path == "/xadrez/todas":
                ponte.painel_visto = time.time()
                d = ponte.partidas.painel()
                d["foco"] = ponte.painel_foco
                return self.reply(200, d)
            if u.path == "/xadrez/estado":
                return self.reply(200, self.partida(marcar=False).estado())
            if u.path == "/foto/ultima":            # a foto que o celular mandou (token na URL)
                if not secrets.compare_digest(arg("t").encode(), ponte.token.encode()):
                    return self.reply(403, {"erro": "token da ponte errado"})
                if not ponte.ultima_foto or not ponte.ultima_foto.exists():
                    return self.reply(404, {"erro": "ainda não tem foto"})
                return self.raw(200, ponte.ultima_foto.read_bytes(), "image/jpeg")
            if u.path == "/webcam/stream":          # <img> nao manda cabecalho: token na URL
                if not secrets.compare_digest(arg("t").encode(), ponte.token.encode()):
                    return self.reply(403, {"erro": "token da ponte errado"})
                return self.mjpeg()
            if not self.authorized():
                return self.reply(403, {"erro": "token da ponte errado: confira PONTE_TOKEN no secrets.h"})
            ponte.partidas.visto(self.dispositivo(), self.nome_aparelho())     # aparelho ligado
            try:
                if u.path == "/ping":
                    return self.reply(200, {"ok": True, "pc": socket.gethostname()})
                if u.path == "/open":
                    what = ponte.open_target(arg("t"))
                elif u.path == "/search":
                    what = ponte.search(arg("site"), arg("q"))
                elif u.path == "/webcam":
                    what = ponte.webcam(arg("a"))
                    print("->", what)
                    return self.reply(200, {"ok": True, "feito": what, "camera": ponte.camera.ativa,
                                            "assistindo": getattr(ponte.camera, "publico", 0)})
                elif u.path.startswith("/robo/"):     # rosto do robo, pela internet
                    code, corpo, tipo = remoto.robo_cmd(ponte.canal, ponte.robo_url, self.path[len("/robo"):])
                    return self.raw(code, corpo, tipo)
                elif u.path == "/robo-canal":         # o proprio robo buscando comandos
                    cmds = ponte.canal.buscar({k: arg(k) for k in ("mood", "board", "ip", "rede") + remoto.SNN_CAMPOS})
                    return self.raw(200, "\n".join(cmds).encode(), "text/plain; charset=utf-8")
                elif u.path == "/llm/ligar":          # on=0: so a rede neural reage (aprende sozinha); on=1 volta
                    if arg("on") in ("0", "1"):
                        ponte.llm_ligado = arg("on") == "1"
                        if ponte.arq_llm:
                            if ponte.llm_ligado:
                                ponte.arq_llm.unlink(missing_ok=True)
                            else:
                                ponte.arq_llm.write_text("LLM desligado: so a rede neural reage", encoding="utf-8")
                        print(f"-> LLM {'LIGADO' if ponte.llm_ligado else 'DESLIGADO: so a rede neural reage'}")
                    return self.reply(200, {"ok": True, "llm": ponte.llm_ligado})
                elif u.path == "/usb/soltar":         # gravar firmware: larga a COM sem parar a ponte
                    if self.publico():
                        return self.reply(404, {"erro": "rota desconhecida"})
                    if not ponte.usb:
                        return self.reply(200, {"ok": True, "usb": False})
                    ponte.usb.soltar(float(arg("s") or 0))
                    for _ in range(30):                # espera a porta fechar de verdade
                        if not ponte.usb.solta or ponte.usb.ser is None:
                            break
                        time.sleep(0.1)
                    print(f"-> cabo USB do robo {'solto para gravar' if ponte.usb.solta else 'de volta'}")
                    return self.reply(200, {"ok": True, "usb": True, "solta": ponte.usb.solta})
                elif u.path == "/robo-memoria":       # o robo nasceu sem memoria: devolve a copia
                    dados = ponte.memoria_ler()
                    if dados is None:
                        return self.reply(404, {"erro": "ainda nao tem copia do cerebro"})
                    print(f"-> cerebro do robo recuperado da copia ({len(dados)} bytes)")
                    return self.raw(200, dados, "application/octet-stream")
                elif u.path == "/app/cfg":
                    return self.reply(200, remoto.cfg_app(ponte.robo_url, ponte.canal))
                elif u.path == "/insta/diag":         # so leitura: o que a aba do Instagram mostra
                    return self.reply(200, ponte.insta.diagnostico())
                elif u.path == "/insta/job":          # a aba do Instagram pergunta (1,5 s)
                    return self.reply(200, {"job": ponte.insta.poll(arg("logado") == "1")})
                elif u.path == "/close":
                    what = ponte.close_target(arg("t"))
                elif u.path == "/page/read":
                    return self.reply(200, {"html": ponte.page_read()})
                elif u.path == "/page/open":
                    what = ponte.page_open(arg("where"))
                elif u.path == "/chess/state":
                    return self.reply(200, self.partida().estado(com_legais=True))
                elif u.path == "/chess/open":
                    what = ponte.abrir_painel(self.dispositivo())
                else:
                    return self.reply(404, {"erro": "rota desconhecida"})
            except ValueError as e:
                return self.reply(400, {"erro": str(e)})
            except (RuntimeError, OSError) as e:
                return self.reply(500, {"erro": str(e)})
            print("->", what)
            self.reply(200, {"ok": True, "feito": what})

        def mjpeg(self):
            if not ponte.camera.ativa:
                return self.reply(409, {"erro": "a câmera está desligada"})
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=quadro")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                for jpg in ponte.camera.quadros():
                    self.wfile.write(b"--quadro\r\nContent-Type: image/jpeg\r\nContent-Length: "
                                     + str(len(jpg)).encode() + b"\r\n\r\n" + jpg + b"\r\n")
            except OSError:
                pass                                  # o app fechou o video

        def do_POST(self):
            if urlparse(self.path).path == "/acesso":   # codigo de acesso -> token (app pela internet)
                n = int(self.headers.get("Content-Length") or 0)
                try:
                    codigo = json.loads(self.rfile.read(min(n, 1000)) or b"{}").get("codigo")
                except ValueError:
                    codigo = None
                if not ponte.acesso:
                    return self.reply(403, {"erro": "acesso pela internet desligado"})
                try:
                    ok = ponte.acesso.verificar(self.ip(), codigo)
                except remoto.Bloqueado as e:
                    return self.reply(429, {"erro": str(e)})
                if not ok:
                    print(f"-> codigo de acesso errado ({self.ip()})")
                    return self.reply(403, {"erro": "código de acesso errado"})
                print(f"-> novo aparelho entrou ({self.ip()})")
                return self.reply(200, {"ok": True, "token": ponte.token})
            if not self.authorized():
                # le (e descarta) o corpo antes de recusar: responder sem ler faz o Windows abortar a conexao
                n = int(self.headers.get("Content-Length") or 0)
                if 0 < n <= MAX_FOTO:
                    self.rfile.read(n)
                return self.reply(403, {"erro": "token da ponte errado: confira PONTE_TOKEN no secrets.h"})
            ponte.partidas.visto(self.dispositivo(), self.nome_aparelho())
            path = urlparse(self.path).path
            n = int(self.headers.get("Content-Length") or 0)
            limite = MAX_FOTO if path == "/foto" else MAX_LLM_BODY if path == "/llm" else MAX_HTML * 2
            if n > limite:
                return self.reply(413, {"erro": "grande demais"})
            if path == "/robo-memoria":             # copia de seguranca do cerebro do robo (binario)
                if n > MAX_MEMORIA:
                    return self.reply(413, {"erro": "grande demais"})
                try:
                    tam = ponte.memoria_guardar(self.rfile.read(n))
                except ValueError as e:
                    return self.reply(400, {"erro": str(e)})
                print(f"-> copia do cerebro do robo guardada ({tam} bytes)")
                return self.reply(200, {"ok": True})
            if path == "/foto":                     # JPEG cru, vindo do app Android
                try:
                    nome = ponte.foto_salvar(self.rfile.read(n))
                except ValueError as e:
                    return self.reply(400, {"erro": str(e)})
                print(f"-> foto recebida do celular: {nome}")
                return self.reply(200, {"ok": True, "foto": nome})
            try:
                body = json.loads(self.rfile.read(n) or b"{}")
                if path == "/llm":
                    r = ponte.llm(body.get("messages"), body.get("tools"))
                    calls = [c.get("function", {}).get("name") for c in r["message"].get("tool_calls") or []]
                    print(f"-> cerebro {r['segundos']}s: " + (f"funcoes {calls}" if calls else "resposta"))
                    return self.reply(200, r)
                if path == "/falar":                  # voz no alto-falante do notebook; volta quando acabar
                    texto = str(body.get("texto") or "")[:2000]
                    try:
                        dur = ponte.voz.falar(texto, espera=VOZ_ESPERA_S)
                    except voz_mod.Ocupada as e:          # outro aparelho falando: este fala no celular
                        return self.reply(409, {"erro": str(e), "ocupada": True})
                    print(f"-> falou no notebook ({dur:.1f}s): {texto[:60]}")
                    return self.reply(200, {"ok": True, "duracao_ms": int(dur * 1000)})
                if path == "/voz/calar":
                    ponte.voz.calar()
                    return self.reply(200, {"ok": True})
                if path == "/page/write":
                    ponte.page_write(body.get("html"))
                    print(f"-> pagina gravada ({ponte.page.stat().st_size} bytes)")
                    return self.reply(200, {"ok": True})
                if path == "/wa/draft":
                    did = ponte.wa_draft(body.get("text"))
                    print("-> WhatsApp: rascunho escrito")
                    return self.reply(200, {"ok": True, "draft_id": did})
                if path == "/wa/send":
                    ponte.wa_send(body.get("draft_id"))
                    print("-> WhatsApp: ENVIADO")
                    return self.reply(200, {"ok": True, "enviado": True})
                if path == "/wa/cancel":
                    ponte.wa_cancel()
                    print("-> WhatsApp: rascunho apagado")
                    return self.reply(200, {"ok": True})
                if path == "/chess/new":
                    s = ponte.xadrez(self.partida(), self.dispositivo(), "nova",
                                     cor=body.get("cor", "brancas"), nivel=body.get("nivel", 3))
                    print(f"-> xadrez: partida nova (voce de {s['voce_joga']}, nivel {s['nivel']}, {s['motor']})")
                    return self.reply(200, s)
                if path == "/insta/resultado":        # a extensao devolve o que fez
                    ponte.insta.resultado(body)
                    return self.reply(200, {"ok": True})
                if path == "/insta/draft":
                    did = ponte.insta_rascunho(body.get("legenda"))
                    print("-> Instagram: publicação pronta, esperando confirmação")
                    return self.reply(200, {"ok": True, "draft_id": did, "legenda": body.get("legenda") or ""})
                if path == "/insta/send":
                    r = ponte.insta_publicar(body.get("draft_id"))
                    print("-> Instagram: PUBLICADO (foto apagada do notebook)")
                    return self.reply(200, {"ok": True, "publicado": True, "concluido": bool(r.get("concluido")),
                                            "foto_apagada": True})
                if path == "/insta/cancel":
                    ponte.insta_cancelar()
                    print("-> Instagram: publicação descartada")
                    return self.reply(200, {"ok": True})
                if path == "/chess/end":
                    s = ponte.xadrez(self.partida(), self.dispositivo(), "encerrar")
                    print("-> xadrez: partida encerrada")
                    return self.reply(200, s)
                if path == "/chess/move":
                    s = ponte.xadrez(self.partida(), self.dispositivo(), "jogar", texto=body.get("lance"))
                    print(f"-> xadrez: voce {s['voce']['san']}" + (f", Zeus {s['zeus']['san']}" if s["zeus"] else ""))
                    return self.reply(200, s)
                return self.reply(404, {"erro": "rota desconhecida"})
            except (ValueError, json.JSONDecodeError) as e:
                return self.reply(400, {"erro": str(e)})
            except (RuntimeError, OSError) as e:
                return self.reply(500, {"erro": str(e)})

        def log_message(self, *a):
            pass

    return Handler


def serve(ponte, host="0.0.0.0"):
    srv = ThreadingHTTPServer((host, ponte.port), make_handler(ponte))
    srv.daemon_threads = True
    return srv


HOTSPOT_PS1 = Path(__file__).with_name("hotspot_pc.ps1")


def hotspot(*args):
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(HOTSPOT_PS1), *args],
                       capture_output=True, text=True, timeout=90,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    return (r.stdout or r.stderr or "").strip()


def manter_hotspot(nome, senha, intervalo=30):
    """Hotspot Movel do PC sempre ligado em 2,4 GHz com o nome fixo do robo (secrets.h).

    O Windows compartilha a internet que estava ativa quando o hotspot ligou: se o PC
    troca de rede (outro WiFi, cabo, celular), religa para compartilhar a nova. Tambem
    religa quando o Windows desliga sozinho (economia de energia sem ninguem conectado).
    """
    rede = None
    while True:
        try:
            st = hotspot("-Status")
            m = re.search(r"internet=(.*)$", st)
            agora = m.group(1).strip() if m else None
            if agora and ("estado=On" not in st or agora != rede):
                print("hotspot do PC:", hotspot("-Nome", nome, "-Senha", senha, "-Reiniciar"))
                rede = agora
        except Exception as e:                    # powershell travou ou sem permissao: tenta na proxima
            print("hotspot do PC:", e)
        time.sleep(intervalo)


def main():
    if sys.platform != "win32":
        sys.exit("esta ponte e para Windows")
    import windows_io
    cfg = load_config()
    partida = xadrez.Partida(engine_path=cfg.get("stockfish") or None, pensar_min=PENSAR_MIN_S)
    ponte = Ponte(windows_io, cfg["token"], int(cfg["porta"]), partida,
                  ollama=cfg["ollama"], modelo=cfg["modelo"], codigo=cfg["codigo_acesso"], robo=cfg["robo"],
                  placar=PLACAR)
    ponte.arq_llm = HERE / "llm_desligado.txt"
    ponte.llm_ligado = not ponte.arq_llm.exists()
    srv = serve(ponte)
    # robo no cabo USB: sem WiFi, o rosto recebe os comandos pela serial ("usb": "COM6", "auto" ou "" = desligado)
    if cfg.get("hotspot"):                       # a rede do robo em qualquer rede em que o PC estiver
        threading.Thread(target=manter_hotspot, args=(cfg["hotspot"], cfg["hotspot_senha"]), daemon=True).start()
    if cfg.get("usb"):
        ponte.usb = remoto.LigacaoUSB(ponte.canal, cfg["usb"])
        threading.Thread(target=ponte.usb.rodar, daemon=True).start()
    instagram_mod.gravar_config_extensao(f"http://127.0.0.1:{ponte.port}", ponte.token)
    threading.Thread(target=ponte.aquecer, daemon=True).start()
    if ponte.voz.disponivel:
        threading.Thread(target=ponte.voz.carregar, daemon=True).start()
    else:
        print("voz do notebook: falta ponte\\vozes\\pt_BR-faber-medium.onnx (baixar_voz.ps1); o Zeus fala pelo celular")
    ip = lan_ip()
    print(f"Zeus: ponte do notebook ouvindo na porta {ponte.port}  (Ctrl+C para parar)")
    print(f"cerebro: Ollama {ponte.modelo} em {ponte.ollama} (aquecendo...)")
    print(f"xadrez: motor {partida.nome_motor()}" + (f" ({partida.engine_path})" if partida.engine_path else ""))
    print("\nColoque no firmware/main/secrets.h:")
    if cfg.get("hotspot"):                        # o robo entra no hotspot do PC, onde o PC e sempre .137.1
        ip = "192.168.137.1"
        print(f'  #define WIFI_SSID   "{cfg["hotspot"]}"')
        print(f'  #define WIFI_PASS   "{cfg["hotspot_senha"]}"')
    print(f'  #define PONTE_URL   "http://{ip}:{ponte.port}"')
    print(f'  #define PONTE_TOKEN "{ponte.token}"')
    print("(ou digite o endereco em Ajustes, na pagina do Zeus no celular)")
    print(f"\nPela internet: https://zeus.appdevtest.shop  (código de acesso: {cfg['codigo_acesso']})")
    print("Downloads:     https://zeus.appdevtest.shop/aplicativos\n")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        partida.fechar()


if __name__ == "__main__":
    main()
