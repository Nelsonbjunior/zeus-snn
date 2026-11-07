"""Simulador do ESP32 do Zeus no PC: serve a MESMA pagina do firmware
(firmware/main/web/index.html) e responde as mesmas rotas, so que imprimindo no
terminal em vez de desenhar no OLED.

Serve para testar a conversa sem o robo: em http://localhost o Chrome libera o
microfone sem mexer em chrome://flags. Cidade e ponte vem de firmware/main/secrets.h.

Uso:  py -3.13 tools\\esp_sim.py [porta]      (padrao 8090) e abra http://localhost:8090
"""
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
PAGE = ROOT / "firmware" / "main" / "web" / "index.html"
SECRETS = ROOT / "firmware" / "main" / "secrets.h"
MOODS = ["neutro", "feliz", "triste", "bravo", "sono", "curioso", "surpreso", "pensando"]


def read_secrets(path=SECRETS):
    if not path.exists():
        path = path.with_name("secrets.example.h")
    text = path.read_text(encoding="utf-8")
    return dict(re.findall(r'^\s*#define\s+(\w+)\s+"([^"]*)"', text, re.M))


def make_handler(state, s):
    class Handler(BaseHTTPRequestHandler):
        def send(self, code, data, ctype):
            body = data if isinstance(data, bytes) else data.encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            u = urlparse(self.path)
            q = {k: v[0] for k, v in parse_qs(u.query).items()}
            ok = lambda: self.send(200, "ok", "text/plain")
            if u.path == "/":
                return self.send(200, PAGE.read_bytes(), "text/html; charset=utf-8")
            if u.path == "/cfg":
                return self.send(200, json.dumps({
                    "lat": s.get("LAT", ""), "lon": s.get("LON", ""),
                    "city": s.get("CIDADE", ""), "ponte": s.get("PONTE_URL", ""),
                    "token": s.get("PONTE_TOKEN", ""), "ip": "127.0.0.1"}), "application/json")
            if u.path == "/state":
                return self.send(200, json.dumps(state), "application/json")
            if u.path == "/mood":
                m = int(q.get("m", 0) or 0)
                state["mood"] = MOODS[m] if 0 <= m < len(MOODS) else "neutro"
                print(f"[rosto] {state['mood']}" + (" (fraco)" if q.get("w") == "1" else "")
                      + (f" segura {q['hold']} ms" if q.get("hold") else ""))
                return self.send(200, json.dumps(state), "application/json")
            if u.path == "/show":
                print(f"[display] {q.get('t', '')!r}  humor={q.get('m')}")
                return ok()
            if u.path == "/talk":
                return ok()
            if u.path == "/think":
                print("[rosto] calculando" + (f" vs {q.get('n')}" if q.get("n") else "")
                      if q.get("on") == "1" else "[rosto] parou de calcular")
                return ok()
            if u.path == "/spk":
                print("[rosto] comecou a responder")
                return ok()
            if u.path == "/board":
                if q.get("off"):
                    state["board"] = 0
                    return ok()
                b = q.get("b", "")
                if len(b) != 64:
                    return self.send(400, "b precisa de 64 casas", "text/plain")
                state["board"] = 1
                rows = [b[i:i + 8] for i in range(0, 64, 8)]
                if q.get("f") == "1":
                    rows = [r[::-1] for r in rows[::-1]]
                side = [q.get("t1", ""), q.get("t2", ""), q.get("t3", "")]
                print("[display] tabuleiro, ultimo lance", q.get("l", "-"))
                for i, r in enumerate(rows):
                    print("   ", " ".join(r), "  ", side[i] if i < 3 else "")
                return ok()
            if u.path in ("/manifest.json", "/icon.svg"):
                return self.send(404, "", "text/plain")
            self.send(404, "rota desconhecida", "text/plain")

        def log_message(self, *a):
            pass

    return Handler


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8090
    state = {"mood": "neutro", "board": 0}
    s = read_secrets()
    srv = ThreadingHTTPServer(("127.0.0.1", port), make_handler(state, s))
    print(f"Zeus simulado em http://localhost:{port}  (Ctrl+C para parar)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
