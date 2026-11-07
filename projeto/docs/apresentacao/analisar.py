"""Analisa a voz real do Zeus ("Oi, tudo bem?") e grava os numeros usados nos slides.

Uso (Python da ponte, com piper-tts e numpy):
    python analisar.py          # reaproveita oi_tudo_bem.wav se existir
    python analisar.py --nova   # sintetiza de novo com a voz Piper
Saida: onda.json (contorno, zoom, espectro, autocorrelacao e o passo a passo).
"""
import io
import json
import os
import sys
import wave

import numpy as np

AQUI = os.path.dirname(os.path.abspath(__file__))
WAV = os.path.join(AQUI, "oi_tudo_bem.wav")
FRASE = "Oi, tudo bem?"
N = 1024                     # tamanho do recorte (potencia de 2 para a FFT)
F0_MIN, F0_MAX = 70, 400     # faixa de voz humana procurada na autocorrelacao


def sintetizar():
    sys.path.insert(0, os.path.join(AQUI, "..", "..", "ponte"))
    import voz
    v = voz.Voz()
    v.carregar()
    dados, _ = v.sintetizar(FRASE)
    with open(WAV, "wb") as f:
        f.write(dados)


def parabola(y, k):
    """Vertice da parabola por (k-1, k, k+1): devolve o deslocamento fracionario."""
    a, b, c = y[k - 1], y[k], y[k + 1]
    return 0.5 * (a - c) / (a - 2 * b + c)


def main():
    if "--nova" in sys.argv or not os.path.exists(WAV):
        sintetizar()
    with wave.open(WAV) as w:
        fs, n = w.getframerate(), w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(float)
    tam = os.path.getsize(WAV)

    # contorno de 10 ms para o grafico da onda
    jan = fs // 100
    blocos = [x[i:i + jan] for i in range(0, n, jan)]
    env_max = [int(b.max()) for b in blocos]
    env_min = [int(b.min()) for b in blocos]

    # energia por bloco de 10 ms em dBFS
    rms = np.array([np.sqrt(np.mean(b ** 2)) for b in blocos])
    dbfs = 20 * np.log10(np.maximum(rms, 1) / 32768)

    # recorte de N amostras centrado no bloco mais forte da vogal de "bem"
    k_forte = int(np.argmax(rms[len(rms) // 2:])) + len(rms) // 2
    ini = max(0, min(n - N, k_forte * jan + jan // 2 - N // 2))
    seg = x[ini:ini + N]

    # passo 2: janela de Hann
    hann = 0.5 * (1 - np.cos(2 * np.pi * np.arange(N) / (N - 1)))
    segw = seg * hann

    # passo 3: FFT
    X = np.fft.rfft(segw)
    mag = np.abs(X)
    mag_db = 20 * np.log10(np.maximum(mag, 1e-9) / mag.max())
    df = fs / N
    k_pico = int(np.argmax(mag[1:])) + 1
    d_pico = parabola(mag_db, k_pico)
    f_pico = (k_pico + d_pico) * df

    # passo 4: autocorrelacao (sem janela, normalizada por r[0])
    s0 = seg - seg.mean()
    r = np.correlate(s0, s0, mode="full")[N - 1:]
    r = r / r[0]
    tau_min, tau_max = int(fs / F0_MAX), int(fs / F0_MIN)
    tau = int(np.argmax(r[tau_min:tau_max + 1])) + tau_min
    d_tau = parabola(r, tau)
    f0 = fs / (tau + d_tau)

    # harmonicos do espectro ate 2 kHz (picos locais acima de -30 dB)
    harm = [round(i * df, 1) for i in range(2, int(2000 / df))
            if mag_db[i] > -30 and mag_db[i] >= mag_db[i - 1] and mag_db[i] >= mag_db[i + 1]]

    res = {
        "frase": FRASE, "sr": fs, "n": n, "dur": n / fs, "bytes": tam,
        "max": env_max, "min": env_min, "dbfs": [round(v, 1) for v in dbfs],
        "zoom": [int(v) for v in seg[N // 2 - 20:N // 2 + 20]],
        "recorte": {"ini": ini, "t_ini": ini / fs, "t_fim": (ini + N) / fs, "N": N, "ms": 1000 * N / fs},
        "fft": {"df": df, "k_pico": k_pico, "d_pico": d_pico, "f_pico": f_pico, "harmonicos": harm,
                "freq": [round(i * df, 1) for i in range(int(2000 / df) + 1)],
                "db": [round(v, 1) for v in mag_db[:int(2000 / df) + 1]]},
        "auto": {"tau_min": tau_min, "tau_max": tau_max, "tau": tau, "d_tau": d_tau,
                 "r_tau": float(r[tau]), "f0": f0, "r": [round(float(v), 3) for v in r[:tau_max + 1]]},
        "pico_dbfs": float(20 * np.log10(np.abs(x).max() / 32768)),
    }
    with open(os.path.join(AQUI, "onda.json"), "w", encoding="utf-8") as f:
        json.dump(res, f)
    print(f"fs={fs} n={n} dur={n / fs:.4f}s bytes={tam}")
    print(f"recorte {ini / fs:.3f}-{(ini + N) / fs:.3f}s ({1000 * N / fs:.1f} ms)")
    print(f"FFT: df={df:.3f} Hz, pico k={k_pico} desl={d_pico:+.3f} -> {f_pico:.1f} Hz; harmonicos {harm}")
    print(f"auto: tau em [{tau_min},{tau_max}] -> tau={tau} desl={d_tau:+.3f} r={r[tau]:.3f} -> f0={f0:.1f} Hz")
    print(f"pico do sinal {res['pico_dbfs']:.1f} dBFS; blocos acima de -40 dBFS: "
          f"{sum(1 for v in dbfs if v > -40)}/{len(dbfs)}")


if __name__ == "__main__":
    main()
