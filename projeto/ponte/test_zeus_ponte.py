"""Testes da ponte do Zeus. Nao abrem app nenhum nem tocam no WhatsApp de verdade:
o modulo windows_io e trocado por um falso que so anota o que seria feito.

Uso:  py -3.13 -m unittest -v test_zeus_ponte.py   (dentro de ponte/)
"""
import json
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

import chess

import cerebro_snn
import xadrez
import zeus_ponte

ORIGIN = "http://192.168.15.201"


class FakeIO:
    def __init__(self):
        self.calls = []
        self.front = ("WhatsApp", "WhatsApp.exe")
        self.clip = "o que eu tinha copiado"
        self.run_result = (0, "")

    def open_uri(self, uri):
        self.calls.append(("uri", uri))

    def spawn(self, argv):
        self.calls.append(("spawn", argv))

    def run(self, argv, timeout=10):
        self.calls.append(("run", argv))
        return self.run_result

    def clip_get(self):
        return self.clip

    def clip_set(self, text):
        self.calls.append(("clip", text))
        self.clip = text

    def keys(self, *names):
        self.calls.append(("keys", names))

    def foreground(self):
        return self.front

    def bring_to_front(self, title):
        self.calls.append(("front", title))
        return False

    # janelas: lista de (hwnd, titulo, exe); focus() poe a janela na frente
    janelas = ()
    frente_hwnd = 0
    deixa_focar = True

    def windows(self):
        return list(self.janelas)

    def focus(self, hwnd):
        self.calls.append(("focus", hwnd))
        if self.deixa_focar:
            self.frente_hwnd = hwnd
            for h, t, e in self.janelas:
                if h == hwnd:
                    self.front = (t, e)
        return self.deixa_focar

    def foreground_hwnd(self):
        return self.frente_hwnd

    def close_window(self, hwnd):
        self.calls.append(("close", hwnd))


class FakeInsta:
    """Instagram de mentira: anota o que seria feito, nunca publica de verdade."""

    def __init__(self):
        self.calls = []
        self.logado = True

    def abrir(self):
        self.calls.append("abrir")
        return {"logado": self.logado, "extensao": True}

    def rascunho(self, foto, legenda):
        self.calls.append(("rascunho", Path(foto).name, legenda))
        return {"ok": True}

    def publicar(self):
        self.calls.append("publicar")
        return {"ok": True, "publicado": True}

    def cancelar(self):
        self.calls.append("cancelar")
        return {"ok": True}


class FakeCamera:
    """Camera de mentira: tres quadros JPEG falsos e depois desliga."""

    def __init__(self):
        self.ativa = False
        self.ligacoes = 0
        self.publico = 0

    def ligar(self):
        if not self.ativa:
            self.ligacoes += 1
        self.ativa = True

    def desligar(self):
        self.ativa = False

    def quadros(self):
        for i in range(3):
            yield b"\xff\xd8quadro%d\xff\xd9" % i
        self.ativa = False


class BaseServer(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.io = FakeIO()
        partida = xadrez.Partida(usar_motor=False, rng=__import__("random").Random(7))
        self.cam = FakeCamera()
        self.insta = FakeInsta()
        self.ponte = zeus_ponte.Ponte(self.io, "segredo", 0, partida, Path(self.tmp.name) / "pagina" / "index.html",
                                      camera=self.cam, insta=self.insta, fotos=Path(self.tmp.name) / "fotos",
                                      memoria=Path(self.tmp.name) / "cerebro")
        self.srv = zeus_ponte.serve(self.ponte, host="127.0.0.1")
        self.ponte.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.base = f"http://127.0.0.1:{self.ponte.port}"

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.tmp.cleanup()

    def call(self, path, body=None, token="segredo", method=None, origin=ORIGIN, disp=None, nome=None, extra=None):
        headers = {"Origin": origin}
        if token is not None:
            headers["X-Zeus-Token"] = token
        if disp:
            headers["X-Zeus-Dispositivo"] = disp
        if nome:
            headers["X-Zeus-Nome"] = urllib.parse.quote(nome)
        headers.update(extra or {})
        data = None
        if body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(self.base + path, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                raw = r.read()
                return r.status, (json.loads(raw) if raw[:1] in (b"{", b"[") else raw), r.headers
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read()), e.headers


class TestSeguranca(BaseServer):
    def test_sem_token_nao_executa(self):
        code, body, _ = self.call("/open?t=whatsapp", token=None)
        self.assertEqual(code, 403)
        code, body, _ = self.call("/chess/new", {"cor": "brancas"}, token="errado")
        self.assertEqual(code, 403)
        self.assertEqual(self.io.calls, [])

    def test_preflight_cors_rede_local(self):
        code, _, h = self.call("/open", method="OPTIONS", token=None)
        self.assertEqual(code, 204)
        self.assertEqual(h["Access-Control-Allow-Origin"], ORIGIN)
        self.assertIn("X-Zeus-Token", h["Access-Control-Allow-Headers"])
        self.assertEqual(h["Access-Control-Allow-Private-Network"], "true")

    def test_cors_negado_para_site_da_internet(self):
        _, _, h = self.call("/ping", origin="https://site-malicioso.com")
        self.assertIsNone(h.get("Access-Control-Allow-Origin"))

    def test_origens(self):
        ok = ["http://192.168.0.201", "http://10.0.0.5", "http://172.20.1.1:8080", "http://localhost:8080"]
        ruins = ["https://192.168.0.1", "http://8.8.8.8", "http://172.32.0.1", "http://evil.com", "", None]
        for o in ok:
            self.assertTrue(zeus_ponte.private_origin(o), o)
        for o in ruins:
            self.assertFalse(zeus_ponte.private_origin(o), o)

    def test_alvo_fora_da_lista(self):
        code, body, _ = self.call("/open?t=cmd.exe")
        self.assertEqual(code, 400)
        code, body, _ = self.call("/search?site=bing&q=x")
        self.assertEqual(code, 400)
        self.assertEqual(self.io.calls, [])


class TestAcoes(BaseServer):
    def test_ping(self):
        code, body, _ = self.call("/ping")
        self.assertEqual(code, 200)
        self.assertTrue(body["ok"])

    def test_abrir_app_e_site(self):
        self.call("/open?t=whatsapp")
        self.call("/open?t=calculadora")
        self.call("/open?t=gmail")
        self.assertEqual(self.io.calls, [("uri", "whatsapp:"), ("spawn", ["calc.exe"]),
                                         ("uri", "https://mail.google.com")])

    def test_pesquisa_codifica_texto(self):
        code, body, _ = self.call("/search?site=youtube&q=" + urllib.request.quote("rock & roll"))
        self.assertEqual(code, 200)
        self.assertEqual(self.io.calls, [("uri", "https://www.youtube.com/results?search_query=rock+%26+roll")])

    def test_webcam_no_app(self):
        self.io.run_result = (0, "INFO: No tasks are running which match the specified criteria.")
        code, body, _ = self.call("/webcam?a=ligar")
        self.assertEqual(body["feito"], "câmera ligada: a imagem aparece no app")
        self.assertTrue(body["camera"])
        self.assertNotIn(("uri", "microsoft.windows.camera:"), self.io.calls)   # nada de app Camera
        code, body, _ = self.call("/webcam?a=desligar")
        self.assertEqual(body["feito"], "câmera desligada")
        self.assertFalse(body["camera"])
        code, body, _ = self.call("/webcam?a=desligar")
        self.assertEqual(body["feito"], "a câmera já estava desligada")
        code, _, _ = self.call("/webcam?a=gravar")
        self.assertEqual(code, 400)

    def test_webcam_nao_fecha_o_app_camera_de_ninguem(self):
        self.io.run_result = (0, 'WindowsCamera.exe            43392 Console    1    307,092 K')
        code, body, _ = self.call("/webcam?a=ligar")
        self.assertEqual(code, 500)
        self.assertIn("app Câmera do Windows está usando", body["erro"])
        self.assertFalse([c for c in self.io.calls if c[0] == "run" and "taskkill" in c[1]])
        self.assertFalse(self.cam.ativa)

    def test_varios_aparelhos_na_mesma_camera(self):
        self.io.run_result = (0, "")
        self.call("/webcam?a=ligar")                       # celular 1
        code, body, _ = self.call("/webcam?a=ligar")       # celular 2: entra, nao desliga
        self.assertTrue(body["camera"])
        self.assertEqual(self.cam.ligacoes, 1)              # uma captura so
        self.cam.publico = 1                                # o celular 1 continua assistindo
        code, body, _ = self.call("/webcam?a=sair")        # celular 2 fecha a janela
        self.assertTrue(body["camera"])
        self.assertIn("continua ligada", body["feito"])
        self.cam.publico = 0                                # o ultimo tambem fechou
        code, body, _ = self.call("/webcam?a=sair")
        self.assertFalse(body["camera"])
        self.assertEqual(body["feito"], "câmera desligada")

    def test_webcam_alternar(self):
        self.io.run_result = (0, "INFO: No tasks are running which match the specified criteria.")
        code, body, _ = self.call("/webcam?a=alternar")
        self.assertTrue(body["camera"])
        code, body, _ = self.call("/webcam?a=alternar")
        self.assertFalse(body["camera"])

    def test_video_mjpeg(self):
        self.io.run_result = (0, "")
        req = urllib.request.Request(self.base + "/webcam/stream?t=errado")
        with self.assertRaises(urllib.error.HTTPError) as cm:
            urllib.request.urlopen(req, timeout=5)
        self.assertEqual(cm.exception.code, 403)
        self.call("/webcam?a=ligar")
        with urllib.request.urlopen(self.base + "/webcam/stream?t=segredo", timeout=5) as r:
            self.assertIn("multipart/x-mixed-replace", r.headers["Content-Type"])
            corpo = r.read()
        self.assertEqual(corpo.count(b"--quadro\r\nContent-Type: image/jpeg"), 3)
        self.assertIn(b"quadro2", corpo)

    def test_fechar_aba_do_youtube(self):
        self.io.janelas = [(1, "secrets.h - Visual Studio Code", "Code.exe"),
                           (2, "(909) YouTube \u2014 Profile 2 \u2014 Mozilla Firefox", "firefox.exe")]
        code, body, _ = self.call("/close?t=youtube")
        self.assertEqual(code, 200)
        self.assertEqual(body["feito"], "aba do youtube fechada")
        self.assertEqual(self.io.calls, [("focus", 2), ("keys", ("ctrl", "w"))])

    def test_fechar_aba_do_instagram_descarta_o_post(self):
        self.ponte.insta_draft = {"id": "abc", "at": time.time()}
        self.io.janelas = [(5, "Criar novo post \u2022 Instagram - Google Chrome", "chrome.exe"),
                           (6, "secrets.h - Visual Studio Code", "Code.exe")]
        code, body, _ = self.call("/close?t=instagram")
        self.assertEqual(body["feito"], "aba do instagram fechada")
        self.assertEqual(self.io.calls, [("focus", 5), ("keys", ("ctrl", "w"))])
        self.assertIsNone(self.ponte.insta_draft["id"])            # nao publica mais depois
        code, body, _ = self.call("/insta/send", {"draft_id": "abc"})
        self.assertEqual(code, 400)

    def test_fechar_aba_nao_aperta_tecla_em_outra_janela(self):
        self.io.janelas = [(2, "YouTube - Google Chrome", "chrome.exe")]
        self.io.deixa_focar = False                       # o Windows nao trocou o foco
        self.io.front = ("secrets.h - Visual Studio Code", "Code.exe")
        code, body, _ = self.call("/close?t=youtube")
        self.assertEqual(code, 500)
        self.assertNotIn(("keys", ("ctrl", "w")), self.io.calls)
        self.io.janelas = [(3, "Documento - Bloco de Notas", "notepad.exe")]
        code, body, _ = self.call("/close?t=youtube")      # YouTube nao esta na frente de nenhuma janela
        self.assertEqual(code, 400)

    def test_fechar_apps(self):
        self.io.janelas = [(1, "Calculator", "ApplicationFrameHost.exe"), (2, "Program Manager", "explorer.exe"),
                           (3, "DEV (D:) - File Explorer", "explorer.exe"), (4, "x - Visual Studio Code", "Code.exe")]
        self.assertEqual(self.call("/close?t=calculadora")[1]["feito"], "Calculadora fechado")
        self.assertEqual(self.call("/close?t=explorador")[1]["feito"], "Explorador de Arquivos fechado")
        self.assertEqual(self.io.calls, [("close", 1), ("close", 3)])     # area de trabalho e VS Code ficam
        self.assertEqual(self.call("/close?t=spotify")[1]["feito"], "Spotify já estava fechado")
        self.assertEqual(self.call("/close?t=vscode")[0], 400)

    def test_pagina_grava_le_e_recarrega(self):
        html = "<!doctype html><html><body><h1>Oi Zeus</h1></body></html>"
        code, body, _ = self.call("/page/write", {"html": html})
        self.assertEqual(code, 200)
        code, body, _ = self.call("/page/read")
        self.assertEqual(body["html"], html)
        code, raw, _ = self.call("/pagina", token=None)
        self.assertIn(b"Oi Zeus", raw)
        self.assertIn(b"/pagina/v", raw)            # script de recarregar entra antes do </body>
        _, v1, _ = self.call("/pagina/v", token=None)
        self.call("/page/write", {"html": html.replace("Oi", "Ola")})
        _, v2, _ = self.call("/pagina/v", token=None)
        self.assertNotEqual(v1, v2)
        code, _, _ = self.call("/page/write", {"html": "  "})
        self.assertEqual(code, 400)


class TestWhatsApp(BaseServer):
    def test_rascunho_envio_e_area_de_transferencia(self):
        code, body, _ = self.call("/wa/draft", {"text": "chego em 10 minutos, não esquece o pão"})
        self.assertEqual(code, 200)
        did = body["draft_id"]
        self.assertIn(("clip", "chego em 10 minutos, não esquece o pão"), self.io.calls)
        self.assertIn(("keys", ("ctrl", "v")), self.io.calls)
        self.assertEqual(self.io.clip, "o que eu tinha copiado")     # devolveu o que tinha
        self.assertNotIn(("keys", ("enter",)), self.io.calls)        # rascunho nao envia
        code, body, _ = self.call("/wa/send", {"draft_id": "outro"})
        self.assertEqual(code, 400)
        code, body, _ = self.call("/wa/send", {"draft_id": did})
        self.assertEqual(code, 200)
        self.assertEqual(self.io.calls[-1], ("keys", ("enter",)))
        code, body, _ = self.call("/wa/send", {"draft_id": did})    # um rascunho envia uma vez so
        self.assertEqual(code, 400)

    def test_rascunho_expira(self):
        _, body, _ = self.call("/wa/draft", {"text": "oi"})
        self.ponte.draft["at"] -= zeus_ponte.WA_TTL + 1
        code, body2, _ = self.call("/wa/send", {"draft_id": body["draft_id"]})
        self.assertEqual(code, 400)
        self.assertIn("expirou", body2["erro"])

    def test_nao_digita_se_whatsapp_nao_estiver_na_frente(self):
        self.io.front = ("Documento - Word", "WINWORD.EXE")
        t = time.time()
        code, body, _ = self.call("/wa/draft", {"text": "oi"})
        self.assertEqual(code, 500)
        self.assertIn("não ficou na frente", body["erro"])
        self.assertFalse([c for c in self.io.calls if c[0] in ("keys", "clip")])
        self.assertLess(time.time() - t, 6)


JPEG = b"\xff\xd8\xff\xe0" + b"foto de teste" * 50 + b"\xff\xd9"


class TestFotoInstagram(BaseServer):
    def enviar_foto(self, dados=JPEG, token="segredo"):
        req = urllib.request.Request(self.base + "/foto", data=dados, method="POST",
                                     headers={"X-Zeus-Token": token, "Content-Type": "image/jpeg"})
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status, json.loads(r.read())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    def test_foto_do_celular(self):
        self.assertEqual(self.enviar_foto(token="errado")[0], 403)
        self.assertEqual(self.enviar_foto(b"nao e jpeg")[0], 400)
        code, body = self.enviar_foto()
        self.assertEqual(code, 200)
        self.assertTrue(body["foto"].startswith("foto_"))
        with urllib.request.urlopen(self.base + "/foto/ultima?t=segredo", timeout=5) as r:
            self.assertEqual(r.headers["Content-Type"], "image/jpeg")
            self.assertEqual(r.read(), JPEG)
        with self.assertRaises(urllib.error.HTTPError):
            urllib.request.urlopen(self.base + "/foto/ultima?t=errado", timeout=5)

    def test_abrir_instagram(self):
        code, body, _ = self.call("/open?t=instagram")
        self.assertEqual(body["feito"], "Instagram aberto no Chrome")
        self.insta.logado = False
        code, body, _ = self.call("/open?t=instagram")
        self.assertIn("entre na sua conta", body["feito"])
        self.assertEqual(self.io.calls, [])                 # nao abriu o navegador padrao

    def test_publicar_so_com_rascunho_e_uma_vez(self):
        code, body, _ = self.call("/insta/draft", {"legenda": "Oi"})
        self.assertEqual(code, 400)                          # sem foto
        self.assertIn("tirar uma foto", body["erro"])
        self.enviar_foto()
        code, body, _ = self.call("/insta/send", {"draft_id": "x"})
        self.assertEqual(code, 400)                          # sem rascunho
        code, body, _ = self.call("/insta/draft", {"legenda": "Bancada do Zeus 🤖"})
        self.assertEqual(code, 200)
        did = body["draft_id"]
        self.assertEqual(self.insta.calls[-1][2], "Bancada do Zeus 🤖")
        self.assertNotIn("publicar", self.insta.calls)       # rascunho nao publica
        self.assertEqual(self.call("/insta/send", {"draft_id": "outro"})[0], 400)
        code, body, _ = self.call("/insta/send", {"draft_id": did})
        self.assertEqual(code, 200)
        self.assertEqual(self.insta.calls.count("publicar"), 1)
        self.assertTrue(body["foto_apagada"])
        self.assertEqual(self.call("/insta/send", {"draft_id": did})[0], 400)   # nao publica duas vezes
        # a foto publicada sumiu: postar de novo exige foto nova
        self.assertEqual(list(self.ponte.fotos.glob("foto_*.jpg")), [])
        with self.assertRaises(urllib.error.HTTPError):
            urllib.request.urlopen(self.base + "/foto/ultima?t=segredo", timeout=5)
        code, body, _ = self.call("/insta/draft", {"legenda": "de novo"})
        self.assertEqual(code, 400)
        self.assertIn("tirar uma foto", body["erro"])

    def test_rascunho_expira_e_cancela(self):
        self.enviar_foto()
        _, body, _ = self.call("/insta/draft", {"legenda": ""})
        self.ponte.insta_draft["at"] -= zeus_ponte.INSTA_TTL + 1
        code, body2, _ = self.call("/insta/send", {"draft_id": body["draft_id"]})
        self.assertEqual(code, 400)
        self.assertIn("expirou", body2["erro"])
        self.call("/insta/cancel", {})
        self.assertEqual(self.insta.calls[-1], "cancelar")
        self.assertNotIn("publicar", self.insta.calls)


class TestExtensaoInstagram(BaseServer):
    """Instagram de verdade (fila da ponte) com uma extensao de mentira numa thread."""

    def setUp(self):
        super().setUp()
        import instagram
        self.aberturas = []
        self.ponte._insta = instagram.Instagram(abrir_url=self.aberturas.append, espera_conexao=3, espera_tarefa=5)
        self.parar = False
        self.feito = []
        self.legenda_vista = None
        threading.Thread(target=self.extensao, daemon=True).start()

    def tearDown(self):
        self.parar = True
        super().tearDown()

    def extensao(self):
        while not self.parar:
            try:
                _, r, _ = self.call("/insta/job?logado=1")
                job = r.get("job")
                if job:
                    self.feito.append(job["acao"])
                    if job["acao"] == "rascunho":
                        self.legenda_vista = job["legenda"]
                    self.call("/insta/resultado", {"id": job["id"], "ok": True})
            except Exception:
                pass
            time.sleep(0.2)

    def test_fluxo_pela_extensao(self):
        time.sleep(0.5)
        code, body, _ = self.call("/open?t=instagram")
        self.assertEqual(body["feito"], "Instagram aberto no Chrome")
        self.assertEqual(self.aberturas, [])                  # a aba ja estava aberta: so foca
        req = urllib.request.Request(self.base + "/foto", data=JPEG, method="POST",
                                     headers={"X-Zeus-Token": "segredo"})
        urllib.request.urlopen(req, timeout=5).read()
        code, body, _ = self.call("/insta/draft", {"legenda": "Zeus postando"})
        self.assertEqual(code, 200)
        self.assertEqual(self.legenda_vista, "Zeus postando")
        self.assertNotIn("publicar", self.feito)
        code, body, _ = self.call("/insta/send", {"draft_id": body["draft_id"]})
        self.assertEqual(code, 200)
        self.assertEqual(self.feito.count("publicar"), 1)

    def test_sem_extensao_abre_o_chrome_e_avisa(self):
        self.parar = True
        time.sleep(7)                                         # a "aba" some
        code, body, _ = self.call("/open?t=instagram")
        self.assertEqual(self.aberturas, ["https://www.instagram.com/"])
        self.assertIn("extensão do Zeus não respondeu", body["feito"])


class FakeRobo:
    """ESP32 de mentira: responde /state e anota o que recebeu."""

    def __init__(self):
        from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
        pedidos = self.pedidos = []

        class H(BaseHTTPRequestHandler):
            def do_GET(self):
                pedidos.append(self.path)
                corpo = b'{"mood":"feliz","board":0}' if self.path.startswith("/state") else b"ok"
                if self.path == "/cfg":
                    corpo = b'{"city":"Linhares","lat":"-19.39","lon":"-40.07","ponte":"http://x","token":"t"}'
                self.send_response(200)
                self.send_header("Content-Length", str(len(corpo)))
                self.end_headers()
                self.wfile.write(corpo)

            def log_message(self, *a):
                pass

        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.url = f"http://127.0.0.1:{self.srv.server_address[1]}"
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def close(self):
        self.srv.shutdown()
        self.srv.server_close()


class TestInternet(BaseServer):
    def setUp(self):
        super().setUp()
        import remoto
        self.robo = FakeRobo()
        self.ponte.robo_url = self.robo.url
        self.ponte.acesso = remoto.Acesso("12345678")

    def tearDown(self):
        self.robo.close()
        super().tearDown()

    def get(self, path, headers=None):
        req = urllib.request.Request(self.base + path, headers=headers or {})
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status, r.read(), r.headers
        except urllib.error.HTTPError as e:
            return e.code, e.read(), e.headers

    def entrar(self, codigo, ip="200.1.2.3"):
        req = urllib.request.Request(self.base + "/acesso", data=json.dumps({"codigo": codigo}).encode(),
                                     method="POST", headers={"Content-Type": "application/json",
                                                             "Cf-Connecting-Ip": ip})
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status, json.loads(r.read())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    def test_app_e_downloads_abertos(self):
        code, corpo, _ = self.get("/")
        self.assertEqual(code, 200)
        self.assertIn(b"window.ZEUS_REMOTO=true", corpo)
        self.assertIn(b"apple-touch-icon", corpo)
        code, corpo, _ = self.get("/aplicativos")
        self.assertEqual(code, 200)
        self.assertIn("Adicionar à Tela de Início".encode(), corpo)
        code, corpo, _ = self.get("/manifest.json")
        self.assertEqual(json.loads(corpo)["display"], "standalone")
        self.assertEqual(self.get("/aplicativos/..%5Czeus_ponte.json")[0], 404)   # nada fora da pasta

    def test_codigo_de_acesso(self):
        code, body = self.entrar("00000000")
        self.assertEqual(code, 403)
        self.assertNotIn("token", body)
        code, body = self.entrar("12345678")
        self.assertEqual(code, 200)
        self.assertEqual(body["token"], "segredo")

    def test_bloqueia_forca_bruta(self):
        for i in range(5):
            self.entrar("1111111%d" % i, ip="9.9.9.9")
        code, body = self.entrar("12345678", ip="9.9.9.9")       # nem o certo passa
        self.assertEqual(code, 429)
        code, body = self.entrar("12345678", ip="8.8.8.8")       # outro aparelho continua entrando
        self.assertEqual(code, 200)

    def test_paginas_do_notebook_nao_vao_para_a_internet(self):
        cf = {"Cf-Connecting-Ip": "200.1.2.3"}
        self.assertEqual(self.get("/pagina", cf)[0], 404)
        self.assertEqual(self.get("/xadrez/estado", cf)[0], 404)
        self.assertEqual(self.get("/xadrez/estado")[0], 200)      # no proprio notebook continua

    def test_rosto_do_robo_pela_internet(self):
        self.assertEqual(self.get("/robo/state")[0], 403)          # sem token
        tok = {"X-Zeus-Token": "segredo"}
        code, corpo, _ = self.get("/robo/state", tok)
        self.assertEqual(json.loads(corpo)["mood"], "feliz")
        self.get("/robo/mood?m=1&hold=6000", tok)
        self.assertIn("/mood?m=1&hold=6000", self.robo.pedidos)
        self.assertEqual(self.get("/robo/../cfg", tok)[0], 404)   # so rotas do rosto
        self.assertEqual(self.get("/robo/reboot", tok)[0], 404)
        code, corpo, _ = self.get("/app/cfg", tok)
        cfg = json.loads(corpo)
        self.assertEqual(cfg["city"], "Linhares")
        self.assertEqual(cfg["ponte"], "")                         # o app usa o endereco https
        self.assertTrue(cfg["robo_online"])

    def test_robo_desligado(self):
        self.robo.close()
        code, corpo, _ = self.get("/robo/state", {"X-Zeus-Token": "segredo"})
        self.assertEqual(code, 502)

    def test_robo_pelo_canal_da_internet(self):
        """Roteador isola os aparelhos: a ponte nao alcanca o robo, mas o robo liga para ela."""
        self.robo.close()
        self.ponte.robo_url = "http://127.0.0.1:9"                 # rede de casa inalcancavel
        tok = {"X-Zeus-Token": "segredo"}
        self.assertEqual(self.get("/robo-canal")[0], 403)           # o robo tambem precisa do token
        self.assertFalse(json.loads(self.get("/app/cfg", tok)[1])["robo_online"])

        recebidos = []

        def robo():                                                # o ESP32 buscando comandos
            for _ in range(2):
                code, corpo, _ = self.get("/robo-canal?mood=sono&board=0&ip=10.0.0.7&rede=Outra", tok)
                recebidos.append((code, corpo.decode()))

        t = threading.Thread(target=robo)
        t.start()
        time.sleep(0.4)
        cfg = json.loads(self.get("/app/cfg", tok)[1])
        self.assertTrue(cfg["robo_online"])
        self.assertEqual((cfg["ip"], cfg["rede"]), ("10.0.0.7", "Outra"))
        self.assertEqual(json.loads(self.get("/robo/state", tok)[1])["mood"], "sono")
        inicio = time.time()
        self.assertEqual(self.get("/robo/mood?m=1&hold=6000", tok)[0], 200)
        self.get("/robo/talk?on=1&p=1", tok)
        t.join(10)
        self.assertLess(time.time() - inicio, 3)                   # o comando nao espera o long-poll
        cmds = "\n".join(c for _, c in recebidos)
        self.assertIn("/mood?m=1&hold=6000", cmds)
        self.assertIn("/talk?on=1&p=1", cmds)
        self.assertEqual(self.get("/robo/reboot", tok)[0], 404)    # so rotas do rosto, tambem no canal

    def test_canal_novo_encerra_o_velho(self):
        import remoto
        canal = remoto.CanalRobo()
        velho = []
        t = threading.Thread(target=lambda: velho.append(canal.buscar({}, espera=5)))
        t.start()
        time.sleep(0.2)
        novo = []
        t2 = threading.Thread(target=lambda: novo.append(canal.buscar({}, espera=5)))
        t2.start()
        t.join(2)
        self.assertEqual(velho, [[]])                              # conexao velha nao leva comandos
        canal.enviar("/show?t=oi")
        t2.join(2)
        self.assertEqual(novo, [["/show?t=oi"]])


class TestXadrezHTTP(BaseServer):
    def test_partida_completa_pelo_http(self):
        code, s, _ = self.call("/chess/new", {"cor": "brancas", "nivel": 3})
        self.assertEqual(code, 200)
        self.assertEqual(s["casas"], "rnbqkbnrpppppppp" + "." * 32 + "PPPPPPPPRNBQKBNR")
        self.assertEqual(s["vez"], "voce")
        code, s, _ = self.call("/chess/move", {"lance": "e4"})
        self.assertEqual(code, 200)
        self.assertEqual(s["voce"]["san"], "e4")
        self.assertEqual(s["voce"]["descricao"], "peão de e2 para e4")
        self.assertIsNotNone(s["zeus"])
        self.assertEqual(s["vez"], "voce")
        self.assertEqual(len(s["casas"]), 64)
        code, s, _ = self.call("/chess/move", {"lance": "Ke5"})
        self.assertEqual(code, 400)
        code, s, _ = self.call("/chess/state")
        self.assertIn("lances_legais", s)
        code, s, _ = self.call("/xadrez/estado", token=None)        # tabuleiro do PC, so leitura
        self.assertEqual(code, 200)

    def test_pretas_zeus_comeca(self):
        code, s, _ = self.call("/chess/new", {"cor": "pretas"})
        self.assertTrue(s["visto_pelas_pretas"])
        self.assertIsNotNone(s["zeus"])
        self.assertEqual(s["vez"], "voce")

    def test_encerrar_partida(self):
        self.call("/chess/new", {"cor": "brancas"})
        code, s, _ = self.call("/chess/end", {})
        self.assertEqual(code, 200)
        self.assertFalse(s["ativa"])
        self.assertEqual(s["resultado"], "partida encerrada")
        code, s, _ = self.call("/chess/move", {"lance": "e4"})
        self.assertEqual(code, 400)
        code, s, _ = self.call("/chess/end", {})              # nao tem mais partida
        self.assertEqual(code, 400)

    def test_sem_partida(self):
        code, s, _ = self.call("/chess/move", {"lance": "e4"})
        self.assertEqual(code, 400)
        self.assertIn("chess_new", s["erro"])


class FakeOllama:
    """Servidor que finge ser o Ollama: anota o pedido e devolve uma chamada de funcao."""

    def __init__(self):
        from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
        pedidos = self.pedidos = []

        class H(BaseHTTPRequestHandler):
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                pedidos.append((self.path, body))
                data = json.dumps({"message": {"role": "assistant", "content": "", "tool_calls": [
                    {"function": {"name": "get_time", "arguments": {}}}]}}).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def log_message(self, *a):
                pass

        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.url = f"http://127.0.0.1:{self.srv.server_address[1]}"
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def close(self):
        self.srv.shutdown()
        self.srv.server_close()


class TestCerebro(BaseServer):
    def test_repassa_ao_ollama(self):
        fake = FakeOllama()
        try:
            self.ponte.ollama, self.ponte.modelo = fake.url, "llama3.1:8b"
            msgs = [{"role": "system", "content": "voce e o Zeus"}, {"role": "user", "content": "que horas são?"}]
            tools = [{"type": "function", "function": {"name": "get_time", "parameters": {"type": "object"}}}]
            code, body, _ = self.call("/llm", {"messages": msgs, "tools": tools})
            self.assertEqual(code, 200)
            self.assertEqual(body["message"]["tool_calls"][0]["function"]["name"], "get_time")
            path, sent = fake.pedidos[0]
            self.assertEqual(path, "/api/chat")
            self.assertEqual(sent["model"], "llama3.1:8b")
            self.assertFalse(sent["stream"])
            self.assertEqual(sent["messages"], msgs)
            self.assertEqual(sent["tools"], tools)
        finally:
            fake.close()

    def test_exige_token_e_conversa(self):
        code, _, _ = self.call("/llm", {"messages": [{"role": "user", "content": "oi"}]}, token=None)
        self.assertEqual(code, 403)
        code, _, _ = self.call("/llm", {"messages": []})
        self.assertEqual(code, 400)

    def test_cerebro_ocupado_nao_trava_para_sempre(self):
        antigo = zeus_ponte.LLM_FILA_S
        zeus_ponte.LLM_FILA_S = 0.5
        self.ponte.llm_lock.acquire()                  # outro pedido preso usando a GPU
        try:
            t = time.time()
            code, body, _ = self.call("/llm", {"messages": [{"role": "user", "content": "oi"}]})
            self.assertEqual(code, 500)
            self.assertIn("ocupado", body["erro"])
            self.assertLess(time.time() - t, 5)
        finally:
            self.ponte.llm_lock.release()
            zeus_ponte.LLM_FILA_S = antigo

    def test_ollama_desligado(self):
        self.ponte.ollama = "http://127.0.0.1:9"            # porta sem ninguem
        code, body, _ = self.call("/llm", {"messages": [{"role": "user", "content": "oi"}]})
        self.assertEqual(code, 500)
        self.assertIn("Ollama", body["erro"])


class TestRedeNeural(BaseServer):
    """A SNN do robo em paralelo com o LLM: fala vira estimulo, e o LLM recebe os sentimentos."""

    def test_fala_vira_estimulo(self):
        self.assertIn(("carinho", 100), cerebro_snn.estimulos_da_fala("Bom garoto, você é demais!"))
        self.assertIn(("bronca", 100), cerebro_snn.estimulos_da_fala("Seu BURRO, cala a boca"))
        e = cerebro_snn.estimulos_da_fala("que horas são?")
        self.assertIn(("som", 100), e)
        self.assertNotIn(("carinho", 100), e)
        self.assertNotIn(("bronca", 100), e)
        self.assertEqual(cerebro_snn.estimulos_da_fala("   "), [])
        self.assertEqual(cerebro_snn.estimulos_das_funcoes(["search_web"]), [("web", 100)])
        self.assertEqual(cerebro_snn.estimulos_das_funcoes(["chess_move", "open"]), [("jogo", 80)])
        self.assertEqual(cerebro_snn.estimulos_das_funcoes(["get_time"]), [])

    def test_descreve_sentimentos(self):
        t = cerebro_snn.descrever({"e": "37.5.2.8.21.3.9", "en": "87", "j": "0", "h": "1"})
        self.assertIn("alegria 37%", t)
        self.assertIn("curiosidade 21%", t)
        self.assertNotIn("tristeza", t)                 # fraca demais para citar
        self.assertIn("energia 87%", t)
        self.assertIn("jeito de ser: alegre", t)
        self.assertIn("cara de feliz", t)
        self.assertIsNone(cerebro_snn.descrever({"mood": "feliz"}))   # robo sem a rede: nada muda
        self.assertIsNone(cerebro_snn.descrever({"e": "1.2.3"}))

    def test_llm_recebe_os_sentimentos_e_a_rede_sente_a_conversa(self):
        fake = FakeOllama()
        enviados = []
        self.ponte.rosto = enviados.append
        try:
            self.ponte.ollama = fake.url
            self.ponte.canal.buscar({"mood": "feliz", "e": "60.0.0.2.20.1.5", "en": "90", "j": "-1", "h": "1"},
                                    espera=0)
            msgs = [{"role": "system", "content": "voce e o Zeus"}, {"role": "user", "content": "obrigado, lindo!"}]
            code, _, _ = self.call("/llm", {"messages": msgs, "tools": []})
            self.assertEqual(code, 200)
            _, sent = fake.pedidos[0]
            self.assertTrue(sent["messages"][0]["content"].startswith("voce e o Zeus"))
            self.assertIn("rede neural", sent["messages"][0]["content"])
            self.assertIn("alegria 60%", sent["messages"][0]["content"])
            self.assertEqual(sent["messages"][1], msgs[1])
            self.assertIn("/snn?s=som&v=100", enviados)
            self.assertIn("/snn?s=carinho&v=100", enviados)
            self.assertIn("/snn?s=trabalho&c=100", enviados)
            self.assertEqual(enviados.count("/snn?s=trabalho&c=0"), 1)
        finally:
            fake.close()

    def test_retorno_de_funcao_nao_e_fala_nova(self):
        fake = FakeOllama()
        enviados = []
        self.ponte.rosto = enviados.append
        try:
            self.ponte.ollama = fake.url
            msgs = [{"role": "user", "content": "seu burro"}, {"role": "tool", "content": "12:00"}]
            code, _, _ = self.call("/llm", {"messages": msgs})
            self.assertEqual(code, 200)
            self.assertFalse([c for c in enviados if "bronca" in c or "som" in c])
        finally:
            fake.close()

    def test_palavrao_e_bronca(self):
        for frase in ("porra, Zeus", "vai se foder", "que merda", "seu filho da puta", "caralho!", "vtnc"):
            self.assertIn(("bronca", 100), cerebro_snn.estimulos_da_fala(frase), frase)
        self.assertNotIn(("bronca", 100), cerebro_snn.estimulos_da_fala("computador novo"))   # "puta" nao em "computador"

    def test_llm_desligado_so_a_rede_reage(self):
        fake = FakeOllama()
        enviados = []
        self.ponte.rosto = enviados.append
        try:
            self.ponte.ollama = fake.url
            code, body, _ = self.call("/llm/ligar?on=0")
            self.assertEqual((code, body["llm"]), (200, False))
            code, body, _ = self.call("/llm", {"messages": [{"role": "user", "content": "porra, seu burro"}]})
            self.assertEqual(code, 200)
            self.assertEqual(body["message"]["content"], "Grrr!")
            self.assertFalse(fake.pedidos)                          # o Ollama nem foi chamado
            self.assertIn("/snn?s=bronca&v=100", enviados)
            self.assertNotIn("/snn?s=trabalho&c=100", enviados)     # sem LLM pensando
            self.ponte.canal.buscar({"mood": "triste", "h": "2"}, espera=0)
            code, body, _ = self.call("/llm", {"messages": [{"role": "user", "content": "oi"}]})
            self.assertEqual(body["message"]["content"], "Snif...")
            code, body, _ = self.call("/llm/ligar?on=1")
            self.assertTrue(body["llm"])
            code, body, _ = self.call("/llm", {"messages": [{"role": "user", "content": "oi"}]})
            self.assertEqual(len(fake.pedidos), 1)                  # ligado de novo: vai ao LLM
        finally:
            fake.close()

    def test_aprende_palavrao_e_pode_falar(self):
        fake = FakeOllama()
        self.ponte.rosto = lambda c: None
        try:
            self.ponte.ollama = fake.url
            self.ponte.canal.buscar({"mood": "bravo", "e": "0.5.40.0.3.1.0", "h": "3"}, espera=0)
            msgs = [{"role": "system", "content": "voce e o Zeus"}, {"role": "user", "content": "porra, que merda"}]
            self.call("/llm", {"messages": msgs})
            self.assertNotIn("palavroes", fake.pedidos[-1][1]["messages"][0]["content"])   # ouviu 1 vez so
            self.call("/llm", {"messages": msgs})                                          # 2a vez: aprendeu
            sistema = fake.pedidos[-1][1]["messages"][0]["content"]
            self.assertIn("porra", sistema)
            self.assertIn("merda", sistema)
            self.assertIn("raiva (40%)", sistema)                    # rede com raiva: solta um agora
            self.assertIn("Nunca xingue pessoas", sistema)
            self.assertIn("Nunca recuse a conversa", sistema)       # palavrao do dono nao e ofensa
            self.assertEqual(self.ponte.vocabulario.aprendidos()[:2], ["porra", "merda"])
            arq = Path(self.tmp.name) / "cerebro" / "palavroes.json"
            self.assertEqual(cerebro_snn.Vocabulario(arq).aprendidos()[:2], ["porra", "merda"])   # guardado
            self.call("/llm/ligar?on=0")                             # sem LLM: o som de bronca leva o palavrao
            code, body, _ = self.call("/llm", {"messages": [{"role": "user", "content": "seu burro"}]})
            self.assertEqual(body["message"]["content"], "Grrr! Porra!")
            self.assertNotIn("retardado", cerebro_snn.USAVEIS)       # insulto com preconceito nunca vira fala
        finally:
            fake.close()

    def test_negacao_nao_e_bronca(self):
        self.assertNotIn(("bronca", 100), cerebro_snn.estimulos_da_fala("você não é burro, viu"))
        self.assertNotIn(("carinho", 100), cerebro_snn.estimulos_da_fala("isso nunca foi perfeito"))
        self.assertIn(("bronca", 100), cerebro_snn.estimulos_da_fala("não gostei do que você fez"))
        self.assertIn(("bronca", 100), cerebro_snn.estimulos_da_fala("não, seu burro"))   # longe da negacao

    def test_copia_do_cerebro_do_robo(self):
        code, _, _ = self.call("/robo-memoria")
        self.assertEqual(code, 404)                                   # ainda nao tem copia
        mem = b"ZSN1" + bytes(range(200)) * 8
        req = urllib.request.Request(self.base + "/robo-memoria", data=mem, method="POST",
                                     headers={"X-Zeus-Token": "segredo", "Content-Type": "application/octet-stream"})
        with urllib.request.urlopen(req, timeout=5) as r:
            self.assertEqual(r.status, 200)
        req = urllib.request.Request(self.base + "/robo-memoria", headers={"X-Zeus-Token": "segredo"})
        with urllib.request.urlopen(req, timeout=5) as r:
            self.assertEqual(r.read(), mem)
        self.assertEqual(len(list((Path(self.tmp.name) / "cerebro").glob("memoria_*.bin"))), 1)
        req = urllib.request.Request(self.base + "/robo-memoria", data=b"lixo", method="POST",
                                     headers={"X-Zeus-Token": "segredo"})
        with self.assertRaises(urllib.error.HTTPError) as e:
            urllib.request.urlopen(req, timeout=5)
        self.assertEqual(e.exception.code, 400)
        req = urllib.request.Request(self.base + "/robo-memoria", data=mem, method="POST")   # sem token
        with self.assertRaises(urllib.error.HTTPError) as e:
            urllib.request.urlopen(req, timeout=5)
        self.assertEqual(e.exception.code, 403)

    def test_estimulo_pelo_app_vai_ao_robo(self):
        self.ponte.canal.buscar({"mood": "neutro"}, espera=0)          # robo ligado no canal
        code, _, _ = self.call("/robo/snn?s=carinho&v=100")
        self.assertEqual(code, 200)
        self.assertEqual(self.ponte.canal.buscar({}, espera=0), ["/snn?s=carinho&v=100"])


class TestLances(unittest.TestCase):
    def board(self, *sans):
        b = chess.Board()
        for s in sans:
            b.push_san(s)
        return b

    def test_formatos(self):
        b = chess.Board()
        casos = {
            "e4": "e2e4", "Nf3": "g1f3", "Cf3": "g1f3", "e2e4": "e2e4", "e2 para e4": "e2e4",
            "cavalo f3": "g1f3", "Cavalo para f3": "g1f3", "peão e4": "e2e4", "Peão d4": "d2d4",
            "cavalo de g1 para f3": "g1f3",
        }
        for txt, uci in casos.items():
            self.assertEqual(xadrez.parse_lance(b, txt).uci(), uci, txt)

    def test_roque_e_captura(self):
        b = self.board("e4", "e5", "Nf3", "Nc6", "Bc4", "Bc5")
        self.assertEqual(xadrez.parse_lance(b, "roque pequeno").uci(), "e1g1")
        self.assertEqual(xadrez.parse_lance(b, "O-O").uci(), "e1g1")
        self.assertEqual(xadrez.parse_lance(b, "0-0").uci(), "e1g1")
        b = self.board("e4", "d5")
        self.assertEqual(xadrez.parse_lance(b, "exd5").uci(), "e4d5")
        self.assertEqual(xadrez.parse_lance(b, "peão e toma d5").uci(), "e4d5")
        self.assertEqual(xadrez.descrever(b, chess.Move.from_uci("e4d5")), "peão de e4 captura peão em d5")
        with self.assertRaises(xadrez.LanceInvalido):
            xadrez.parse_lance(b, "roque grande")

    def test_ambiguo_e_ilegal(self):
        b = chess.Board("4k3/8/8/8/8/8/4K3/R6R w - - 0 1")     # as duas torres chegam em d1
        with self.assertRaises(xadrez.LanceInvalido) as cm:
            xadrez.parse_lance(b, "Rd1")
        self.assertIn("ambíguo", str(cm.exception))
        self.assertEqual(xadrez.parse_lance(b, "Rad1").uci(), "a1d1")
        with self.assertRaises(xadrez.LanceInvalido):
            xadrez.parse_lance(chess.Board(), "e5")
        with self.assertRaises(xadrez.LanceInvalido):
            xadrez.parse_lance(chess.Board(), "")

    def test_promocao(self):
        b = chess.Board("8/4P3/8/8/8/8/k7/4K3 w - - 0 1")
        self.assertEqual(xadrez.parse_lance(b, "e7e8").uci(), "e7e8q")          # toque no celular: dama
        self.assertEqual(xadrez.parse_lance(b, "e8=N").uci(), "e7e8n")
        self.assertEqual(xadrez.parse_lance(b, "e8 promove a torre").uci(), "e7e8r")
        self.assertEqual(xadrez.san_pt("e8=Q+"), "e8=D+")
        self.assertEqual(xadrez.san_pt("Nxe5"), "Cxe5")
        self.assertEqual(xadrez.san_pt("Kf1"), "Rf1")

    def test_mate_do_pastor(self):
        p = xadrez.Partida(usar_motor=False)
        p.nova("brancas")
        p.board = self.board("e4", "e5", "Bc4", "Nc6", "Qh5", "Nf6")
        s = p.jogar("Dxf7")
        self.assertTrue(s["fim"])
        self.assertEqual(s["placa"], "VOCE!")
        self.assertIn("você venceu", s["resultado"])
        self.assertIn("xeque-mate", s["voce"]["descricao"])
        self.assertIsNone(s["zeus"])
        with self.assertRaises(xadrez.LanceInvalido):
            p.jogar("a3")


class TestXadrezPorAparelho(BaseServer):
    ANA, BIA = "aparelho-ana-0001", "aparelho-bia-0002"

    def test_cada_aparelho_tem_a_sua_partida(self):
        self.call("/chess/new", {"cor": "brancas"}, disp=self.ANA)
        code, s, _ = self.call("/chess/new", {"cor": "pretas"}, disp=self.BIA)
        self.assertTrue(s["visto_pelas_pretas"])
        _, a, _ = self.call("/chess/move", {"lance": "e4"}, disp=self.ANA)
        self.assertEqual(a["voce"]["san"], "e4")
        _, b, _ = self.call("/chess/state", disp=self.BIA)            # a Bia nao viu o lance da Ana
        self.assertEqual(b["voce_joga"], "pretas")
        self.assertIsNone(b["voce"])
        self.assertNotEqual(a["fen"], b["fen"])
        self.call("/chess/end", {}, disp=self.ANA)                    # a Ana encerra; a Bia continua
        self.assertFalse(self.call("/chess/state", disp=self.ANA)[1]["ativa"])
        self.assertTrue(self.call("/chess/state", disp=self.BIA)[1]["ativa"])
        self.assertFalse(self.call("/chess/state")[1]["ativa"])       # sem codigo: a partida padrao

    def test_tabuleiro_do_notebook_mostra_a_partida_do_aparelho(self):
        self.call("/chess/new", {"cor": "pretas"}, disp=self.BIA)
        _, s, _ = self.call(f"/xadrez/estado?d={self.BIA}", token=None)
        self.assertTrue(s["ativa"] and s["visto_pelas_pretas"])
        self.call("/chess/open", disp=self.BIA)
        self.assertTrue(any(f"/xadrez?d={self.BIA}" in str(a) for a in self.io.calls))

    def test_codigo_estranho_cai_na_partida_padrao(self):
        self.call("/chess/new", {"cor": "pretas"}, disp="../../x")
        self.assertTrue(self.call("/chess/state")[1]["visto_pelas_pretas"])

    def pastor(self, disp):
        """Deixa a partida do aparelho a um lance do mate do pastor (brancas, vez do aparelho)."""
        self.call("/chess/new", {"cor": "brancas"}, disp=disp)
        p = self.ponte.partidas.de(disp)
        for lance in ("e4", "e5", "Bc4", "Nc6", "Qh5", "Nf6"):
            p.board.push_san(lance)

    def test_oled_mostra_zeus_calculando_com_o_nome_e_depois_o_lance(self):
        rosto = []
        self.ponte.rosto = rosto.append
        self.call("/chess/new", {"cor": "brancas"}, disp=self.ANA, nome="Galaxy S21 Ultra")
        self.assertEqual(rosto, [])                                    # de brancas: o Zeus nao joga ainda
        _, s, _ = self.call("/chess/move", {"lance": "e4"}, disp=self.ANA, nome="Galaxy S21 Ultra")
        self.assertEqual(rosto[0], "/think?on=1&n=Galaxy%20S21%20Ultra")
        self.assertEqual(rosto[1], "/think?on=0")
        self.assertEqual(rosto[2], "/show?t=" + urllib.parse.quote(s["zeus"]["san_pt"]) + "&m=1")
        self.assertFalse(any(r.startswith("/board") for r in rosto))  # tabuleiro nao vai ao OLED
        rosto.clear()
        self.call("/chess/move", {"lance": "Ke5"}, disp=self.ANA)      # ilegal: nem comeca a calcular
        self.assertEqual(rosto, [])

    def test_placar_e_painel_do_notebook(self):
        self.ponte.partidas.arquivo = Path(self.tmp.name) / "placar.json"
        self.pastor(self.ANA)
        self.call("/chess/state", disp=self.ANA, nome="Galaxy S21")
        _, s, _ = self.call("/chess/move", {"lance": "Dxf7"}, disp=self.ANA)
        self.assertIn("você venceu", s["resultado"])
        self.call("/chess/state", disp=self.ANA)                       # o fim conta uma vez so
        self.call("/chess/new", {"cor": "pretas"}, disp=self.BIA, nome="iPhone da Bia")
        _, d, _ = self.call("/xadrez/todas", token=None)
        self.assertEqual(d["placar"], {"zeus": 0, "aparelho": 1, "empates": 0})
        self.assertEqual((d["ligados"], d["jogando"]), (2, 1))
        nomes = [a["nome"] for a in d["aparelhos"]]
        self.assertEqual(nomes, ["iPhone da Bia", "Galaxy S21"])        # jogando primeiro
        ana = d["aparelhos"][1]
        self.assertEqual(ana["placar"]["aparelho"], 1)
        self.assertTrue(ana["partida"]["fim"])
        # o placar fica no disco: a ponte reiniciada lembra
        outra = xadrez.Partidas(xadrez.Partida(usar_motor=False), self.ponte.partidas.arquivo)
        self.assertEqual(outra.info[self.ANA]["aparelho"], 1)
        self.assertEqual(outra.info[self.ANA]["nome"], "Galaxy S21")
        # painel e so do notebook
        cf = {"Cf-Connecting-Ip": "200.1.2.3"}
        self.assertEqual(self.call("/xadrez/todas", token=None, extra=cf)[0], 404)
        self.assertEqual(self.call("/xadrez", token=None, extra=cf)[0], 404)
        code, html, _ = self.call("/xadrez", token=None)
        self.assertEqual(code, 200)
        self.assertIn(b"/xadrez/todas", html)

    def test_painel_mostra_zeus_pensando(self):
        p = self.ponte.partidas.de(self.ANA, "Moto G")
        p.pensar_min = 0.6
        self.call("/chess/new", {"cor": "brancas"}, disp=self.ANA)
        t = threading.Thread(target=lambda: self.call("/chess/move", {"lance": "d4"}, disp=self.ANA))
        t.start()
        time.sleep(0.3)
        _, d, _ = self.call("/xadrez/todas", token=None)               # nao espera o lance acabar
        a = d["aparelhos"][0]
        self.assertTrue(a["pensando"])
        self.assertEqual(a["partida"]["vez"], "zeus")
        self.assertEqual(a["partida"]["voce"]["san"], "d4")
        t.join(5)
        self.assertFalse(self.call("/xadrez/todas", token=None)[1]["aparelhos"][0]["pensando"])

    def test_ver_no_notebook_reaproveita_o_painel_aberto(self):
        focados = []
        self.io.windows = lambda: [(7, "Gmail - Google Chrome", "chrome.exe"), (42, "Zeus - xadrez", "chrome.exe")]
        self.io.focus = focados.append
        _, r, _ = self.call("/chess/open", disp=self.ANA)           # painel fechado: abre (modo app)
        self.assertIn("no Chrome", r["feito"])
        abriu = [c for c in self.io.calls if "xadrez" in str(c)]
        self.assertTrue(any("--app=" in str(c) or c[0] == "uri" for c in abriu))
        self.call("/xadrez/todas", token=None)                        # o painel abriu e consultou
        n = len(self.io.calls)
        _, r, _ = self.call("/chess/open", disp=self.BIA)
        self.assertEqual(r["feito"], "painel do xadrez na frente")
        self.assertEqual(focados, [42])
        self.assertEqual(len(self.io.calls), n)                       # nenhum Chrome novo
        _, d, _ = self.call("/xadrez/todas", token=None)
        self.assertEqual(d["foco"]["id"], self.BIA[:16])               # o painel rola ate a Bia

    def test_nome_do_aparelho_e_limpo(self):
        self.assertEqual(xadrez.limpar_nome('  <b>Galaxy</b>\n "S21"  '), "bGalaxy/b S21")
        self.assertEqual(len(xadrez.limpar_nome("x" * 80)), 24)

    def test_esquece_partidas_velhas_e_limita(self):
        ps = xadrez.Partidas(xadrez.Partida(usar_motor=False))
        a = ps.de("aparelho-velho-01")
        a.usado -= ps.ESQUECER_S + 1
        self.assertIsNot(ps.de("aparelho-velho-01"), a)               # esquecida: comeca do zero
        for i in range(ps.MAX + 5):
            ps.de(f"aparelho-{i:04d}-xx")
        self.assertLessEqual(len(ps.todas), ps.MAX)


@unittest.skipUnless(xadrez.find_stockfish(), "Stockfish nao encontrado")
class TestStockfish(unittest.TestCase):
    def test_motor_responde(self):
        p = xadrez.Partida()
        try:
            s = p.nova("brancas", 5)
            s = p.jogar("e4")
            self.assertEqual(s["motor"], "stockfish")
            self.assertTrue(chess.Board(s["fen"]).is_valid())
            self.assertIsNotNone(s["zeus"])
        finally:
            p.fechar()

    def test_duas_partidas_ao_mesmo_tempo_no_mesmo_motor(self):
        p = xadrez.Partida()
        q = p.irma()
        try:
            p.nova("brancas", 4)
            q.nova("brancas", 6)
            res = {}
            ts = [threading.Thread(target=lambda: res.__setitem__("p", p.jogar("e4"))),
                  threading.Thread(target=lambda: res.__setitem__("q", q.jogar("d4")))]
            [t.start() for t in ts]
            [t.join(30) for t in ts]
            self.assertEqual(res["p"]["voce"]["san"], "e4")
            self.assertEqual(res["q"]["voce"]["san"], "d4")
            self.assertIsNotNone(res["p"]["zeus"])
            self.assertIsNotNone(res["q"]["zeus"])
            self.assertIs(p._m.engine, q._m.engine)                     # um Stockfish so
        finally:
            p.fechar()


class TestConfig(unittest.TestCase):
    def test_cria_token_na_primeira_vez(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "zeus_ponte.json"
            c1 = zeus_ponte.load_config(path)
            self.assertGreater(len(c1["token"]), 16)
            self.assertEqual(c1["porta"], 8770)
            self.assertEqual(c1["modelo"], "llama3.1:8b")
            c2 = zeus_ponte.load_config(path)
            self.assertEqual(c1["token"], c2["token"])


class FakeVoz:
    """voz.Voz sem Piper nem alto-falante: a sintese devolve 1 s de 'audio' e tocar so espera."""

    def __new__(cls, boca):
        import voz

        class V(voz.Voz):
            def carregar(self):
                return True

            def sintetizar(self, texto):
                return b"wav", 0.6

            def tocar(self, dados, dur):
                self.tocados.append(dados)
                self._calar.wait(dur)

        v = V(boca=boca, modelo=__file__)          # modelo "existe": disponivel
        v.tocados = []
        return v


class TestVoz(BaseServer):
    def setUp(self):
        super().setUp()
        self.boca = []
        self.ponte.voz = FakeVoz(boca=self.boca.append)

    def test_normaliza_o_que_vai_ser_falado(self):
        import voz
        self.assertEqual(voz.normalizar("São 20h13."), "São 20 horas e 13.")
        self.assertEqual(voz.normalizar("São 9h00"), "São 9 horas")
        self.assertEqual(voz.normalizar("O bitcoin está em R$ 612.300, subiu 2,1%."),
                         "O bitcoin está em 612300 reais, subiu 2,1 por cento.")
        self.assertEqual(voz.normalizar("**Oi!**"), "Oi!")
        self.assertEqual(voz.normalizar("São 00:43."), "São 0 horas e 43.")
        self.assertEqual(voz.normalizar("cerca de R$ 427.460,00, com"), "cerca de 427460 reais, com")

    def test_marcas_de_palavra_cobrem_a_fala(self):
        import voz
        m = voz.marcas_de_palavra("Boa noite, tudo bem com você?", 2.0)
        self.assertEqual(len(m), 6)
        self.assertEqual(m, sorted(m))
        self.assertLess(m[-1], 2.0)

    def test_falar_no_notebook_mexe_a_boca_e_espera_acabar(self):
        t = time.time()
        code, body, _ = self.call("/falar", {"texto": "Boa noite! Tudo bem?"})
        self.assertEqual(code, 200)
        self.assertGreaterEqual(time.time() - t, 0.55)                 # so volta quando a fala acaba
        self.assertEqual(body["duracao_ms"], 600)
        self.assertEqual(self.boca[0], "/talk?on=1")
        self.assertGreaterEqual(self.boca.count("/talk?on=1&p=1"), 3)
        self.assertEqual(self.boca[-1], "/talk?on=0")
        self.assertEqual(self.call("/falar", {"texto": "oi"}, token=None)[0], 403)

    def test_calar_corta_a_fala(self):
        self.ponte.voz.sintetizar = lambda texto: (b"wav", 10.0)
        threading.Timer(0.3, lambda: self.call("/voz/calar", {})).start()
        t = time.time()
        code, _, _ = self.call("/falar", {"texto": "uma fala bem comprida"})
        self.assertEqual(code, 200)
        self.assertLess(time.time() - t, 3)

    def test_voz_ocupada_nao_faz_fila(self):
        """Duas partidas: o segundo aparelho nao espera a fala do primeiro; fala no celular."""
        self.ponte.voz.sintetizar = lambda texto: (b"wav", 4.0)
        t = threading.Thread(target=lambda: self.call("/falar", {"texto": "fala comprida do primeiro"}))
        t.start()
        time.sleep(0.3)
        inicio = time.time()
        code, body, _ = self.call("/falar", {"texto": "Joguei peão de e7 para e5."})
        self.assertEqual(code, 409)
        self.assertTrue(body["ocupada"])
        self.assertLess(time.time() - inicio, 2.5)                    # espera no maximo VOZ_ESPERA_S
        t.join(6)

    def test_sem_voz_instalada_avisa(self):
        import voz
        self.ponte.voz = voz.Voz(modelo=Path(self.tmp.name) / "nao_existe.onnx")
        code, body, _ = self.call("/falar", {"texto": "oi"})
        self.assertEqual(code, 500)
        self.assertIn("voz do notebook", body["erro"])


class FakeSerial:
    def __init__(self):
        self.entrada = b""
        self.escrito = b""
        self.fechada = False

    def read(self, n):
        dados, self.entrada = self.entrada[:n], self.entrada[n:]
        return dados

    def write(self, dados):
        if self.fechada:
            raise OSError("cabo solto")
        self.escrito += dados

    def close(self):
        self.fechada = True


class TestLigacaoUSB(unittest.TestCase):
    """Robo sem WiFi no cabo USB: os comandos do rosto vao pela serial."""

    def setUp(self):
        import remoto
        self.remoto = remoto
        self.canal = remoto.CanalRobo()
        self.ser = FakeSerial()
        self.usb = remoto.LigacaoUSB(self.canal, "COM6", abrir=lambda porta: self.ser)
        self.usb.ESPERA_S = 0.05

    def test_solta_a_porta_para_gravar_e_pega_de_volta(self):
        self.usb.passo()
        self.assertIsNotNone(self.usb.ser)
        self.usb.soltar(30)
        self.usb.passo()
        self.assertIsNone(self.usb.ser)                # porta livre para o esptool
        self.usb.passo()
        self.assertIsNone(self.usb.ser)
        self.usb.soltar(0)
        self.usb.passo()
        self.assertIsNotNone(self.usb.ser)             # de volta sem reiniciar a ponte

    def test_sem_wifi_entrega_comandos_pela_serial(self):
        self.ser.entrada = b"I (99) zeus: log qualquer\n@ZEUS mood=neutro board=0 net=0 ip= rede=\n"
        self.canal.enviar("/mood?m=1")
        self.canal.enviar("/show?t=oi%20Zeus")
        self.usb.passo()
        self.assertEqual(self.ser.escrito, b"/mood?m=1\n/show?t=oi%20Zeus\n")
        self.assertTrue(self.canal.online)                 # o app ve o robo online
        self.assertEqual(self.canal.info()["rede"], "cabo USB")
        code, corpo, _ = self.remoto.robo_cmd(self.canal, "http://127.0.0.1:9", "/state")
        self.assertEqual((code, json.loads(corpo)["mood"]), (200, "neutro"))

    def test_com_wifi_o_cabo_so_escuta(self):
        self.ser.entrada = b"@ZEUS mood=feliz board=0 net=1 ip=10.83.190.201 rede=Ricardo_S23\n"
        self.canal.enviar("/mood?m=1")
        self.usb.passo()
        self.assertEqual(self.ser.escrito, b"")
        self.assertEqual(self.canal.fila, ["/mood?m=1"])    # fica para o robo buscar pelo WiFi
        self.assertEqual(self.usb.estado["rede"], "Ricardo_S23")

    def test_sem_comando_avisa_que_o_cabo_segue_ligado(self):
        self.ser.entrada = b"@ZEUS mood=neutro board=0 net=0 ip= rede=\n"
        self.usb.passo()
        self.assertEqual(self.ser.escrito, b"/usb\n")
        self.usb.passo()                                   # menos de 2 s depois: nao repete
        self.assertEqual(self.ser.escrito, b"/usb\n")

    def test_linha_quebrada_e_nome_de_rede_codificado(self):
        self.ser.entrada = b"@ZEUS mood=triste board=1 net=0 ip= rede=Ser%20Wi-fi"
        self.usb.passo()
        self.assertFalse(self.usb.ativo)                   # linha ainda incompleta
        self.ser.entrada = b"\n"
        self.usb.passo()
        self.assertEqual((self.usb.estado["mood"], self.usb.estado["rede"]), ("triste", "Ser Wi-fi"))

    def test_robo_mudo_ou_porta_ocupada(self):
        self.ser.entrada = b"@ZEUS mood=neutro board=0 net=0 ip= rede=\n"
        self.usb.passo()
        self.usb.visto -= 10                               # parou de falar (reiniciando, cabo solto)
        self.canal.enviar("/mood?m=2")
        self.assertEqual(self.usb.passo(), 0.2)
        self.assertNotIn(b"/mood?m=2", self.ser.escrito)

        def ocupada(porta):
            raise OSError("acesso negado")                 # idf.py flash segurando a porta
        usb = self.remoto.LigacaoUSB(self.canal, "COM6", abrir=ocupada)
        self.assertEqual(usb.passo(), 5)


if __name__ == "__main__":
    unittest.main()
