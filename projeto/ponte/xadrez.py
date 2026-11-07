"""Xadrez do Zeus: quem escolhe o lance e o motor (Stockfish), nunca o modelo de linguagem.

O modelo so traduz a fala do usuario em lance e narra o resultado que volta daqui.
Sem Stockfish, um jogador simples (captura o que vale mais, senao sorteia) assume.
"""
import json
import os
import random
import re
import shutil
import threading
import time
import unicodedata
from pathlib import Path

import chess
import chess.engine

HERE = Path(__file__).resolve().parent

NOME_PT = {chess.PAWN: "peão", chess.KNIGHT: "cavalo", chess.BISHOP: "bispo",
           chess.ROOK: "torre", chess.QUEEN: "dama", chess.KING: "rei"}
LETRA_PT = {"N": "C", "B": "B", "R": "T", "Q": "D", "K": "R"}
PT_PARA_EN = {"C": "N", "B": "B", "T": "R", "D": "Q", "R": "K"}
PALAVRA_PECA = [("cavalo", "N"), ("bispo", "B"), ("torre", "R"), ("dama", "Q"),
                ("rainha", "Q"), ("rei", "K"), ("peao", "")]
VALOR = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9, chess.KING: 0}
FIM_PT = {
    chess.Termination.CHECKMATE: "xeque-mate",
    chess.Termination.STALEMATE: "afogamento",
    chess.Termination.INSUFFICIENT_MATERIAL: "material insuficiente",
    chess.Termination.SEVENTYFIVE_MOVES: "regra dos 75 lances",
    chess.Termination.FIVEFOLD_REPETITION: "repetição de posição",
    chess.Termination.FIFTY_MOVES: "regra dos 50 lances",
    chess.Termination.THREEFOLD_REPETITION: "repetição de posição",
}


class LanceInvalido(ValueError):
    pass


def sem_acento(s):
    return "".join(c for c in unicodedata.normalize("NFD", s) if unicodedata.category(c) != "Mn")


def find_stockfish(explicit=None):
    cands = [explicit, os.environ.get("ZEUS_STOCKFISH"), HERE / "stockfish.exe",
             Path(r"D:\chess_simulator\stockfish.exe"), shutil.which("stockfish")]
    for c in cands:
        if c and Path(c).is_file():
            return str(c)
    return None


def san_pt(san):
    """Nf3 -> Cf3, Qxd8+ -> Dxd8+, e8=Q -> e8=D. Roque fica igual."""
    if san[:1] in LETRA_PT:
        san = LETRA_PT[san[0]] + san[1:]
    return re.sub(r"=([NBRQ])", lambda m: "=" + LETRA_PT[m.group(1)], san)


def descrever(board, move):
    """Descricao falada do lance, calculada ANTES de jogar (board na posicao anterior)."""
    if board.is_castling(move):
        txt = "roque grande" if chess.square_file(move.to_square) == 2 else "roque pequeno"
    else:
        peca = NOME_PT[board.piece_type_at(move.from_square)]
        de, para = chess.square_name(move.from_square), chess.square_name(move.to_square)
        if board.is_capture(move):
            alvo = board.piece_type_at(move.to_square) or chess.PAWN   # en passant: casa vazia
            txt = f"{peca} de {de} captura {NOME_PT[alvo]} em {para}"
        else:
            txt = f"{peca} de {de} para {para}"
        if move.promotion:
            txt += f" e promove a {NOME_PT[move.promotion]}"
    depois = board.copy(stack=False)
    depois.push(move)
    if depois.is_checkmate():
        txt += ", xeque-mate"
    elif depois.is_check():
        txt += ", xeque"
    return txt


def _legal(board, move):
    return move if move in board.legal_moves else None


def _de_coordenadas(board, a, b, promo=""):
    try:
        move = chess.Move.from_uci(a + b + promo)
    except ValueError:
        return None
    piece = board.piece_at(move.from_square)
    # lance tocado no celular sem escolher peca de promocao: vira dama
    if not promo and piece and piece.piece_type == chess.PAWN and chess.square_rank(move.to_square) in (0, 7):
        move.promotion = chess.QUEEN
    return _legal(board, move)


def _san(board, s):
    try:
        return board.parse_san(s)
    except chess.AmbiguousMoveError:
        raise LanceInvalido(f"o lance {s} é ambíguo: diga também a coluna ou a casa de origem")
    except ValueError:
        return None


def parse_lance(board, texto):
    """Aceita SAN em ingles (Nf3), SAN em portugues (Cf3), coordenadas (e2e4, 'e2 para e4'),
    palavras ('cavalo f3', 'roque pequeno', 'peão d toma e5'). Devolve um chess.Move legal."""
    t = (texto or "").strip()
    if not t:
        raise LanceInvalido("diga o lance")
    norm = sem_acento(t).lower()

    if "roque" in norm or re.fullmatch(r"[o0]-?[o0](-?[o0])?[+#]?", norm):
        grande = "grande" in norm or "longo" in norm or norm.replace("-", "").replace("0", "o").startswith("ooo")
        m = _san(board, "O-O-O" if grande else "O-O")
        if m:
            return m
        raise LanceInvalido("o roque não é possível agora")

    # 1) SAN em ingles como veio (o modelo foi instruido a mandar assim)
    compacto = t.replace(" ", "")
    m = _san(board, compacto)
    if m:
        return m

    # 2) origem e destino: "e2e4", "e2 para e4", "cavalo de g1 para f3"
    casas = re.findall(r"([a-h])\s*([1-8])", norm)
    promo = ""
    pm = re.search(r"(?:=|promove\w*\s+(?:a\s+|para\s+)?)(dama|rainha|torre|bispo|cavalo|[qrbn])\b", norm)
    if pm:
        promo = {"dama": "q", "rainha": "q", "torre": "r", "bispo": "b", "cavalo": "n"}.get(pm.group(1), pm.group(1))
    if len(casas) >= 2:
        m = _de_coordenadas(board, "".join(casas[0]), "".join(casas[1]), promo)
        if m:
            return m

    # 3) SAN em portugues: Cf3, Txe1, Dh5, Re2
    if compacto[:1] in PT_PARA_EN:
        m = _san(board, PT_PARA_EN[compacto[0]] + compacto[1:])
        if m:
            return m

    # 4) palavras: "cavalo f3", "bispo toma c4", "peão d toma e5", "peão e4"
    if len(casas) == 1:
        destino = "".join(casas[0])
        base = norm[:pm.start()] if pm else norm          # "promove a torre": torre nao e quem anda
        letra = next((l for w, l in PALAVRA_PECA if w in base), None)
        captura = re.search(r"\b([a-h])\s+(?:toma|captura|come|x)\b", base)
        # so uma casa e nenhuma pista ("Ke5" ilegal) ja foi tentado como SAN: nao vira lance de peao
        if letra is not None or captura or promo:
            if not letra:                                  # peao
                san = (captura.group(1) + "x" + destino) if captura else destino
            else:
                san = letra + destino
            if promo:
                san += "=" + promo.upper()
            m = _san(board, san)
            if m:
                return m

    raise LanceInvalido(f"não entendi ou não é legal agora: {t}")


class Motor:
    """Um Stockfish so para todas as partidas: cada lance pede a vez (leva 0,1 a 0,6 s)."""

    def __init__(self, path):
        self.path = path
        self.engine = None
        self.lock = threading.Lock()


class Partida:
    def __init__(self, engine_path=None, usar_motor=True, rng=None, motor=None, pensar_min=0.0):
        self._m = motor or Motor(find_stockfish(engine_path) if usar_motor else None)
        self.pensar_min = pensar_min               # s minimos "calculando" (para o OLED mostrar)
        self.pensando = False                      # o Zeus esta escolhendo o lance agora
        self.contada = True                        # o resultado ja foi para o placar
        self.foto = None                           # ultimo estado, lido sem esperar o lock (painel)
        self.ao_pensar = None                      # avisa o OLED: ao_pensar(True/False)
        self.lock = threading.Lock()
        self.board = chess.Board()
        self.cor_usuario = chess.WHITE
        self.nivel = 3
        self.ativa = False
        self.ult_voce = None
        self.ult_zeus = None
        self.ult_uci = ""
        self.versao = 0
        self.rng = rng or random.Random()
        self.usado = time.time()                   # para apagar partidas esquecidas

    def irma(self):
        """Partida nova e independente que usa o mesmo Stockfish."""
        return Partida(motor=self._m, pensar_min=self.pensar_min)

    @property
    def engine_path(self):
        return self._m.path

    # ---- motor (chamar com self._m.lock) ----
    def _motor(self):
        if self._m.engine is None and self._m.path:
            try:
                self._m.engine = chess.engine.SimpleEngine.popen_uci(self._m.path)
            except Exception as e:                 # sem motor, joga o simples
                print(f"[xadrez] Stockfish nao abriu ({e}); usando jogador simples")
                self._m.path = None
        return self._m.engine

    def nome_motor(self):
        return "stockfish" if self.engine_path else "simples"

    def _lance_simples(self):
        moves = list(self.board.legal_moves)
        caps = [m for m in moves if self.board.is_capture(m)]
        if caps:
            return max(caps, key=lambda m: VALOR.get(self.board.piece_type_at(m.to_square) or chess.PAWN, 1))
        return self.rng.choice(moves)

    def _escolher(self):
        # niveis 1 e 2: as vezes joga qualquer coisa, para dar chance a iniciantes
        if self.nivel <= 2 and self.rng.random() < (0.4 if self.nivel == 1 else 0.2):
            return self.rng.choice(list(self.board.legal_moves))
        with self._m.lock:
            eng = self._motor()
            if eng:
                try:
                    eng.configure({"Skill Level": round((self.nivel - 1) * 20 / 9)})
                    r = eng.play(self.board, chess.engine.Limit(time=0.1 + self.nivel * 0.05))
                    if r.move:
                        return r.move
                except chess.engine.EngineError as e:
                    print(f"[xadrez] erro do motor ({e}); lance simples nesta jogada")
                except chess.engine.EngineTerminatedError:
                    self._m.engine = None
        return self._lance_simples()

    def _jogar_zeus(self):
        self.pensando = True
        self.foto = self._estado()
        if self.ao_pensar:
            self.ao_pensar(True)
        t0 = time.monotonic()
        try:
            move = self._escolher()
            falta = self.pensar_min - (time.monotonic() - t0)
            if falta > 0:
                time.sleep(falta)
        finally:
            self.pensando = False
            if self.ao_pensar:
                self.ao_pensar(False)
        self.ult_zeus = {"san": self.board.san(move), "descricao": descrever(self.board, move)}
        self.ult_zeus["san_pt"] = san_pt(self.ult_zeus["san"])
        self.ult_uci = move.uci()
        self.board.push(move)

    # ---- API ----
    def nova(self, cor="brancas", nivel=3):
        with self.lock:
            self.board = chess.Board()
            self.cor_usuario = chess.BLACK if str(cor).lower().startswith("pret") else chess.WHITE
            try:
                self.nivel = min(10, max(1, int(nivel)))
            except (TypeError, ValueError):
                self.nivel = 3
            self.ativa = True
            self.contada = False
            self.ult_voce = self.ult_zeus = None
            self.ult_uci = ""
            if self.cor_usuario == chess.BLACK:
                self._jogar_zeus()
            self.versao += 1
            self.foto = self._estado()
            return self.foto

    def jogar(self, texto):
        with self.lock:
            if not self.ativa:
                raise LanceInvalido("não tem partida em andamento: comece com chess_new")
            if self.board.is_game_over(claim_draw=True):
                raise LanceInvalido("a partida já acabou: comece outra com chess_new")
            if self.board.turn != self.cor_usuario:      # nao deveria acontecer
                self._jogar_zeus()
            move = parse_lance(self.board, texto)
            self.ult_voce = {"san": self.board.san(move), "descricao": descrever(self.board, move)}
            self.ult_voce["san_pt"] = san_pt(self.ult_voce["san"])
            self.ult_uci = move.uci()
            self.board.push(move)
            self.ult_zeus = None
            if not self.board.is_game_over(claim_draw=True):
                self.versao += 1
                self._jogar_zeus()
            self.versao += 1
            self.foto = self._estado()
            return self.foto

    def encerrar(self):
        """Fim da partida a pedido: o tabuleiro sai da tela, o motor fica pronto para outra."""
        with self.lock:
            if not self.ativa:
                raise LanceInvalido("não tem partida em andamento")
            self.ativa = False
            self.versao += 1
            s = self.foto = self._estado()
            s["resultado"] = "partida encerrada"
            return s

    def estado(self, com_legais=False):
        with self.lock:
            s = self._estado()
            if com_legais and self.ativa and not s["fim"]:
                s["lances_legais"] = sorted(self.board.san(m) for m in self.board.legal_moves)
            return s

    def _estado(self):
        b = self.board
        casas = "".join((b.piece_at(chess.square(f, r)).symbol() if b.piece_at(chess.square(f, r)) else ".")
                        for r in range(7, -1, -1) for f in range(8))
        out = b.outcome(claim_draw=True)
        fim = out is not None
        if not fim:
            resultado, placa = "", ("XEQUE!" if b.is_check() else "")
        elif out.winner is None:
            resultado, placa = f"empate por {FIM_PT.get(out.termination, 'regra')}", "EMPATE"
        elif out.winner == self.cor_usuario:
            resultado, placa = f"você venceu por {FIM_PT.get(out.termination, 'regra')}", "VOCE!"
        else:
            resultado, placa = f"Zeus venceu por {FIM_PT.get(out.termination, 'regra')}", "MATE!"
        return {
            "ok": True,
            "ativa": self.ativa,
            "casas": casas,
            "fen": b.fen(),
            "visto_pelas_pretas": self.cor_usuario == chess.BLACK,
            "voce_joga": "pretas" if self.cor_usuario == chess.BLACK else "brancas",
            "nivel": self.nivel,
            "vez": "voce" if b.turn == self.cor_usuario else "zeus",
            "lance_numero": b.fullmove_number,
            "ultimo_lance_uci": self.ult_uci,
            "voce": self.ult_voce,
            "zeus": self.ult_zeus,
            "xeque": b.is_check(),
            "fim": fim,
            "resultado": resultado,
            "placa": placa,
            "motor": self.nome_motor(),
            "versao": self.versao,
        }

    def fechar(self):
        with self._m.lock:
            if self._m.engine:
                try:
                    self._m.engine.quit()
                except Exception:
                    pass
                self._m.engine = None


def limpar_nome(nome):
    """Nome do aparelho vindo do app: texto curto e imprimivel."""
    n = re.sub(r"[\x00-\x1f<>&\"']", "", str(nome or "")).strip()
    return re.sub(r"\s+", " ", n)[:24]


class Partidas:
    """Uma partida por aparelho do app (cada celular joga a sua contra o Zeus).

    O aparelho se identifica por um codigo aleatorio guardado no proprio app e manda tambem o
    nome dele (ex.: "Galaxy S21 Ultra"). Sem codigo (pagina antiga, tabuleiro do notebook sem
    ?d=), vale a partida `padrao`. Partidas paradas ha mais de ESQUECER_S somem; com mais de
    MAX, sai a parada ha mais tempo. O placar (Zeus x cada aparelho) fica em `arquivo` e
    sobrevive a reinicios da ponte.
    """
    ESQUECER_S = 7 * 24 * 3600
    MAX = 30
    LIGADO_S = 90              # aparelho que falou com a ponte ha menos que isso esta "ligado"
    INFO_MAX = 200

    def __init__(self, padrao, arquivo=None):
        self.padrao = padrao
        self.arquivo = Path(arquivo) if arquivo else None
        self.lock = threading.Lock()
        self.todas = {}        # aparelho -> Partida
        self.info = {}         # aparelho -> {nome, visto, zeus, aparelho, empates}
        self._carregar()

    # ---- placar em disco ----
    def _carregar(self):
        if not self.arquivo or not self.arquivo.exists():
            return
        try:
            dados = json.loads(self.arquivo.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return
        for d, i in (dados.get("aparelhos") or {}).items():
            self.info[d] = {"nome": limpar_nome(i.get("nome")), "visto": 0.0,
                            **{k: int(i.get(k) or 0) for k in ("zeus", "aparelho", "empates")}}

    def _salvar(self):
        if not self.arquivo:
            return
        dados = {"aparelhos": {d: {k: i[k] for k in ("nome", "zeus", "aparelho", "empates")}
                               for d, i in self.info.items() if i["zeus"] or i["aparelho"] or i["empates"]}}
        tmp = self.arquivo.with_suffix(".tmp")
        try:
            tmp.write_text(json.dumps(dados, ensure_ascii=False, indent=1), encoding="utf-8")
            os.replace(tmp, self.arquivo)
        except OSError as e:
            print(f"[xadrez] nao salvei o placar ({e})")

    def _info(self, d):
        i = self.info.get(d)
        if i is None:
            if len(self.info) >= self.INFO_MAX:          # esquece quem sumiu ha mais tempo
                del self.info[min(self.info, key=lambda k: self.info[k]["visto"])]
            i = self.info[d] = {"nome": "", "visto": 0.0, "zeus": 0, "aparelho": 0, "empates": 0}
        return i

    # ---- aparelhos ----
    def visto(self, dispositivo, nome=None):
        """Todo pedido do app passa aqui: marca o aparelho como ligado e guarda o nome."""
        if not dispositivo:
            return
        with self.lock:
            i = self._info(dispositivo)
            i["visto"] = time.time()
            nome = limpar_nome(nome)
            if nome:
                i["nome"] = nome

    def nome(self, dispositivo):
        with self.lock:
            i = self.info.get(dispositivo)
            return (i and i["nome"]) or "celular"

    def de(self, dispositivo, nome=None):
        if not dispositivo:
            return self.padrao
        self.visto(dispositivo, nome)
        agora = time.time()
        with self.lock:
            for d in [d for d, p in self.todas.items() if agora - p.usado > self.ESQUECER_S]:
                del self.todas[d]
            p = self.todas.get(dispositivo)
            if p is None:
                if len(self.todas) >= self.MAX:
                    del self.todas[min(self.todas, key=lambda d: self.todas[d].usado)]
                p = self.todas[dispositivo] = self.padrao.irma()
            p.usado = agora
            return p

    def registrar(self, dispositivo, partida):
        """Partida que acabou entra no placar (uma vez so). Devolve 'zeus', 'aparelho' ou 'empate'."""
        if not dispositivo or partida.contada:
            return None
        out = partida.board.outcome(claim_draw=True)
        if out is None:
            return None
        partida.contada = True
        quem = "empates" if out.winner is None else "aparelho" if out.winner == partida.cor_usuario else "zeus"
        with self.lock:
            self._info(dispositivo)[quem] += 1
            self._salvar()
        return {"empates": "empate"}.get(quem, quem)

    def ativas(self):
        with self.lock:
            return sum(1 for p in self.todas.values() if p.ativa)

    # ---- painel do notebook ----
    def painel(self):
        """Todas as partidas e o placar, para o navegador do notebook (le sem esperar o motor)."""
        agora = time.time()
        with self.lock:
            # aparelhos ligados e os que tem partida na tela (em andamento ou terminada)
            ids = {d for d, p in self.todas.items() if p.foto and (p.foto["ativa"] or p.foto["fim"])} | \
                  {d for d, i in self.info.items() if agora - i["visto"] < self.LIGADO_S}
            linhas = []
            for d in ids:
                i = self.info.get(d) or {"nome": "", "visto": 0.0, "zeus": 0, "aparelho": 0, "empates": 0}
                p = self.todas.get(d)
                foto = p.foto if p else None
                linhas.append({
                    "id": d[:16],
                    "nome": i["nome"] or "celular",
                    "ligado": agora - i["visto"] < self.LIGADO_S,
                    "pensando": bool(p and p.pensando),
                    "partida": foto if foto and (foto["ativa"] or foto["fim"]) else None,
                    "placar": {k: i[k] for k in ("zeus", "aparelho", "empates")},
                    "_ordem": (not (agora - i["visto"] < self.LIGADO_S), not (foto and foto["ativa"] and not foto["fim"]),
                               (i["nome"] or "celular").lower()),
                })
            total = {k: sum(i[k] for i in self.info.values()) for k in ("zeus", "aparelho", "empates")}
        linhas.sort(key=lambda x: x.pop("_ordem"))
        return {"placar": total, "ligados": sum(1 for x in linhas if x["ligado"]),
                "jogando": sum(1 for x in linhas if x["partida"] and x["partida"]["ativa"] and not x["partida"]["fim"]),
                "aparelhos": linhas}
