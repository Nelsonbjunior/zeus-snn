"""Webcam do notebook para o app: captura com OpenCV numa thread e entrega JPEGs
para o video MJPEG da ponte (/webcam/stream).

Privacidade: a camera so liga quando pedem ("liga a webcam") e desliga sozinha se
ninguem estiver assistindo por SEM_PUBLICO_S segundos.

Varios aparelhos assistem ao mesmo tempo: uma captura so, cada um recebe os mesmos quadros.
Outro PROGRAMA do notebook (app Camera, Meet...) nao divide a webcam com o Zeus: o driver
desta webcam entrega imagem a um programa por vez (testado com DirectShow e Media Foundation).
"""
import threading
import time

LARGURA, ALTURA = 640, 480
FPS = 12
QUALIDADE_JPEG = 70
SEM_PUBLICO_S = 15        # o ultimo aparelho fechou o video: desliga logo (a luz apaga)


class Camera:
    def __init__(self, indice=0):
        self.indice = indice
        self.lock = threading.Condition()
        self.jpeg = None
        self.seq = 0
        self.ativa = False
        self.erro = ""
        self.publico = 0
        self.sem_publico_desde = 0.0
        self._thread = None
        self._parar = False

    def ligar(self):
        """Abre a camera (ou entra na que ja esta ligada). Devolve depois do 1o quadro.

        Varios aparelhos podem pedir ao mesmo tempo: so o primeiro abre a webcam, os outros
        esperam a MESMA captura (duas capturas da mesma webcam travavam a segunda)."""
        with self.lock:
            if self.ativa:
                return
            t = self._thread
            if t and t.is_alive() and self._parar:
                while t.is_alive():                  # captura anterior ainda fechando
                    self.lock.wait(0.1)
                t = None
            if not (t and t.is_alive()):
                self._parar = False
                self.erro = ""
                self.jpeg = None
                self.sem_publico_desde = time.time()
                self._thread = threading.Thread(target=self._loop, daemon=True)
                self._thread.start()
            fim = time.time() + 8
            while self.jpeg is None and not self.erro and time.time() < fim:
                self.lock.wait(0.2)
            if self.ativa:
                return
            if self.erro:
                raise RuntimeError(self.erro)
            if self.jpeg is None:
                self._parar = True
                raise RuntimeError("a webcam não mandou imagem: outro programa do notebook "
                                   "(app Câmera, Meet, Zoom...) está usando a câmera")
            self.ativa = True

    def desligar(self):
        with self.lock:
            self._parar = True
            self.ativa = False
            self.lock.notify_all()
        t = self._thread
        if t and t is not threading.current_thread():
            t.join(timeout=3)

    def _loop(self):
        try:
            import cv2
        except ImportError:
            with self.lock:
                self.erro = "falta o OpenCV na ponte: py -3.13 -m pip install opencv-python"
                self.lock.notify_all()
            return
        cap = cv2.VideoCapture(self.indice, cv2.CAP_DSHOW)       # DirectShow abre rapido no Windows
        try:
            if not cap.isOpened():
                with self.lock:
                    self.erro = "não consegui abrir a webcam do notebook"
                    self.lock.notify_all()
                return
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, LARGURA)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, ALTURA)
            intervalo = 1.0 / FPS
            for _ in range(10):                       # primeiros quadros saem escuros (exposicao)
                cap.read()
            while not self._parar:
                t0 = time.time()
                ok, quadro = cap.read()
                if not ok:
                    time.sleep(0.05)
                    continue
                ok, buf = cv2.imencode(".jpg", quadro, [cv2.IMWRITE_JPEG_QUALITY, QUALIDADE_JPEG])
                if ok:
                    with self.lock:
                        self.jpeg = buf.tobytes()
                        self.seq += 1
                        self.lock.notify_all()
                # ninguem assistindo ha muito tempo: desliga (a luz da camera apaga)
                if self.ativa and self.publico == 0 and time.time() - self.sem_publico_desde > SEM_PUBLICO_S:
                    print("-> webcam desligada: ninguém assistindo")
                    with self.lock:
                        self._parar = True
                        self.ativa = False
                        self.lock.notify_all()
                    break
                time.sleep(max(0.0, intervalo - (time.time() - t0)))
        finally:
            cap.release()

    def quadros(self):
        """Gerador de JPEGs para um espectador; termina quando a camera desliga."""
        with self.lock:
            self.publico += 1
        visto = -1
        try:
            while True:
                with self.lock:
                    while self.ativa and self.seq == visto:
                        self.lock.wait(2)
                    if not self.ativa:
                        return
                    visto, jpg = self.seq, self.jpeg
                yield jpg
        finally:
            with self.lock:
                self.publico -= 1
                if self.publico == 0:
                    self.sem_publico_desde = time.time()
