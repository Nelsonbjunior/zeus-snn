"""Instagram pelo Chrome NORMAL do usuario (logado com a conta dele), via a extensao do Zeus
(ponte/extensao_instagram).

Por que extensao: um navegador aberto por automacao (Playwright) cai no reCAPTCHA da Meta no
login. A extensao roda dentro do Chrome de sempre, sem modo automatizado.

Como conversa: a aba do Instagram (script da extensao) pergunta a ponte a cada 1,5 s se tem
tarefa (/insta/job); a ponte entrega uma tarefa por vez (rascunho, publicar, cancelar) e espera o
resultado (/insta/resultado). Se nenhuma aba responde, a ponte abre o Instagram no Chrome.
"""
import json
import secrets
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXTENSAO = HERE / "extensao_instagram"
URL = "https://www.instagram.com/"
CONECTADO_S = 6          # a aba avisa a cada 1,5 s; sem aviso por 6 s = desconectada


class InstagramErro(RuntimeError):
    pass


def gravar_config_extensao(ponte_url, token, pasta=EXTENSAO):
    """A extensao le o endereco e o token da ponte deste arquivo (fica fora do git)."""
    pasta.mkdir(exist_ok=True)
    (pasta / "config.js").write_text(
        "// gerado pela ponte do Zeus ao iniciar - nao edite\n"
        f"self.ZEUS = {json.dumps({'ponte': ponte_url, 'token': token})};\n", encoding="utf-8")


class Instagram:
    def __init__(self, abrir_url, espera_conexao=25, espera_tarefa=120):
        self.abrir_url = abrir_url             # abre URL no Chrome normal
        self.espera_conexao = espera_conexao
        self.espera_tarefa = espera_tarefa
        self.cv = threading.Condition()
        self.tarefa = None                     # {"id", "acao", ...} ainda nao entregue
        self.resultados = {}
        self.visto = 0.0
        self.logado = None
        self.rascunho_pronto = False

    # ---- lado da extensao (rotas /insta/job e /insta/resultado) ----
    def poll(self, logado):
        with self.cv:
            self.visto = time.time()
            self.logado = logado
            self.cv.notify_all()
            t, self.tarefa = self.tarefa, None    # cada tarefa vai para uma aba so
            return t

    def resultado(self, dados):
        with self.cv:
            self.resultados[dados.get("id")] = dados
            self.cv.notify_all()

    def conectado(self):
        return time.time() - self.visto < CONECTADO_S

    # ---- lado da ponte ----
    def _esperar_conexao(self):
        with self.cv:
            if self.conectado():
                return True
        self.abrir_url(URL)
        fim = time.time() + self.espera_conexao
        with self.cv:
            while not self.conectado() and time.time() < fim:
                self.cv.wait(0.5)
            return self.conectado()

    def _executar(self, acao, **dados):
        if not self._esperar_conexao():
            raise InstagramErro("abri o Instagram no Chrome, mas a extensão do Zeus não respondeu: "
                                "instale a extensão (pasta ponte\\extensao_instagram) e deixe a aba do Instagram aberta")
        tid = secrets.token_hex(4)
        with self.cv:
            self.tarefa = {"id": tid, "acao": acao, **dados}
            fim = time.time() + self.espera_tarefa
            while tid not in self.resultados and time.time() < fim:
                self.cv.wait(0.5)
            if self.tarefa and self.tarefa["id"] == tid:
                self.tarefa = None                 # ninguem pegou: nao deixa para depois
            r = self.resultados.pop(tid, None)
        if r is None:
            raise InstagramErro("o Instagram não respondeu a tempo; confira a aba no Chrome")
        if not r.get("ok"):
            raise InstagramErro(r.get("erro") or "falhou no Instagram")
        return r

    def abrir(self):
        if self.conectado():
            self._executar("focar")
        else:
            self._esperar_conexao()
        return {"logado": bool(self.logado), "extensao": self.conectado()}

    def rascunho(self, foto, legenda):
        if self.logado is False and self.conectado():
            raise InstagramErro("o Instagram não está logado no Chrome: entre na sua conta na aba do Instagram")
        r = self._executar("rascunho", legenda=legenda, nome=Path(foto).name)
        self.rascunho_pronto = True
        return r

    def publicar(self):
        if not self.rascunho_pronto:
            raise InstagramErro("não há publicação pronta: prepare o rascunho antes")
        self.rascunho_pronto = False               # um rascunho publica uma vez so
        return self._executar("publicar")

    def diagnostico(self):
        return self._executar("diagnostico")

    def cancelar(self):
        self.rascunho_pronto = False
        if not self.conectado():
            return {"ok": True}
        return self._executar("cancelar")
