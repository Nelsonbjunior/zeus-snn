"""Voz do Zeus no alto-falante do notebook: Piper (voz neural pt-BR, roda offline, sem chave).

A pagina manda o texto (POST /falar) e espera a fala acabar, para so entao voltar a ouvir;
assim o microfone do celular nao escuta o proprio Zeus. Enquanto fala, a boca do robo
(OLED) abre a cada palavra: o Piper nao da a hora de cada palavra, entao elas sao
espalhadas pela duracao do audio conforme o tamanho e a pontuacao.
"""
import io
import os
import re
import tempfile
import threading
import time
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODELO = HERE / "vozes" / "pt_BR-faber-medium.onnx"
VELOCIDADE = 0.95          # length_scale do Piper: menor = mais rapido


def normalizar(texto):
    """O que o modelo escreve para ler na tela vira algo que a voz fala direito."""
    t = str(texto or "")
    t = re.sub(r"[*_#`>|]", "", t)
    t = re.sub(r"\b(\d{1,2})h(\d{2})\b", lambda m: f"{int(m[1])} horas e {int(m[2])}"
               if int(m[2]) else f"{int(m[1])} horas", t)
    t = re.sub(r"\b(\d{1,2})h\b", r"\1 horas", t)
    t = re.sub(r"\b([01]?\d|2[0-3]):([0-5]\d)\b", lambda m: f"{int(m[1])} horas e {int(m[2])}"
               if int(m[2]) else f"{int(m[1])} horas", t)
    t = re.sub(r"(\d),00\b", r"\1", t)                         # R$ 427.460,00 -> sem centavos
    while re.search(r"(\d)\.(\d{3})\b", t):                  # 612.300 -> 612300 (milhar)
        t = re.sub(r"(\d)\.(\d{3})\b", r"\1\2", t)
    t = re.sub(r"R\$\s*(\d+(?:[.,]\d+)*)(\s*(mil|milh[õo]es|bilh[õo]es))?",
               lambda m: f"{m[1]}{m[2] or ''} reais", t)
    t = t.replace("%", " por cento").replace("°C", " graus").replace("°", " graus")
    return re.sub(r"\s+", " ", t).strip()


def marcas_de_palavra(texto, duracao):
    """Instantes (s) em que cada palavra comeca, espalhados pela duracao do audio."""
    palavras = re.findall(r"\w+[^\w\s]*", texto)
    if not palavras or duracao <= 0:
        return []
    pesos = [len(re.sub(r"\W", "", p)) + (6 if p[-1] in ".!?" else 3 if p[-1] in ",;:" else 1)
             for p in palavras]
    total, acum, marcas = sum(pesos), 0, []
    for p in pesos:
        marcas.append(0.05 + duracao * 0.95 * acum / total)
        acum += p
    return marcas


class Ocupada(RuntimeError):
    """O notebook ja esta falando para outro aparelho."""


class Voz:
    """Fala uma coisa por vez. `boca(caminho)` manda /talk ao robo (sem bloquear)."""

    def __init__(self, boca=None, modelo=MODELO):
        self.boca = boca or (lambda caminho: None)
        self.modelo = Path(modelo)
        self._voz = None
        self._carga = threading.Lock()
        self._fala = threading.Lock()
        self._calar = threading.Event()

    @property
    def disponivel(self):
        return self.modelo.exists()

    def carregar(self):
        with self._carga:
            if self._voz is None:
                from piper import PiperVoice
                self._voz = PiperVoice.load(str(self.modelo))
                self.sintetizar("ok")                    # a primeira sintese e lenta: aquece
        return self._voz

    def sintetizar(self, texto):
        from piper import SynthesisConfig
        buf = io.BytesIO()
        with wave.open(buf, "wb") as w:
            self._voz.synthesize_wav(texto, w, syn_config=SynthesisConfig(length_scale=VELOCIDADE))
        dados = buf.getvalue()
        with wave.open(io.BytesIO(dados)) as w:
            return dados, w.getnframes() / w.getframerate()

    def tocar(self, dados, dur):                         # trocado nos testes
        """Toca e espera acabar. Da memoria o Windows so toca sem poder parar: vai por arquivo."""
        import winsound
        fd, caminho = tempfile.mkstemp(prefix="zeus_voz_", suffix=".wav")
        try:
            with os.fdopen(fd, "wb") as f:
                f.write(dados)
            winsound.PlaySound(caminho, winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT)
            if self._calar.wait(dur + 0.15):
                winsound.PlaySound(None, 0)
        finally:
            try:
                os.remove(caminho)
            except OSError:
                pass

    def parar_som(self):
        pass                                             # tocar() para sozinho quando _calar e marcado

    def falar(self, texto, espera=None):
        """Fala e so volta quando acabar (ou quando calar() for chamado). Devolve a duracao (s).
        espera: segundos maximos aguardando outra fala terminar (None = sem limite); passou,
        levanta Ocupada. Com varias partidas ao mesmo tempo, a fila deixava cada resposta mais
        atrasada (2,5 -> 4,1 -> 5,7 s); quem acha a voz ocupada fala no proprio celular."""
        texto = normalizar(texto)
        if not texto:
            return 0.0
        if not self.disponivel:
            raise RuntimeError("a voz do notebook não está instalada (falta " + self.modelo.name + ")")
        self.carregar()
        if not self._fala.acquire(timeout=-1 if espera is None else espera):
            raise Ocupada("a voz do notebook está falando para outro aparelho")
        try:
            self._calar.clear()
            dados, dur = self.sintetizar(texto)
            fim = threading.Event()
            threading.Thread(target=self._mexer_boca, args=(texto, dur, fim), daemon=True).start()
            try:
                self.tocar(dados, dur)
            finally:
                fim.set()
                self.boca("/talk?on=0")
            return dur
        finally:
            self._fala.release()

    def calar(self):
        self._calar.set()
        try:
            self.parar_som()
        except Exception:
            pass

    def _mexer_boca(self, texto, dur, fim):
        inicio = time.monotonic()
        self.boca("/talk?on=1")
        ultimo_ping = inicio
        for m in marcas_de_palavra(texto, dur):
            while True:
                agora = time.monotonic()
                if fim.is_set() or self._calar.is_set():
                    return
                if agora - ultimo_ping >= 1.0:           # olhos "respiram" enquanto fala
                    self.boca("/talk?on=1")
                    ultimo_ping = agora
                falta = inicio + m - agora
                if falta <= 0:
                    break
                fim.wait(min(falta, 0.2))
            self.boca("/talk?on=1&p=1")
