"""Acesso ao Windows usado pela ponte: abrir apps/URIs, area de transferencia,
teclas e checagem da janela em primeiro plano. Tudo por ctypes (biblioteca padrao).

Os testes trocam este modulo por um falso: nada aqui roda durante os testes.
"""
import ctypes
import os
import subprocess
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

CF_UNICODETEXT = 13
GMEM_MOVEABLE = 0x0002
KEYEVENTF_KEYUP = 0x0002
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
VK = {"ctrl": 0x11, "alt": 0x12, "enter": 0x0D, "backspace": 0x08, "a": 0x41, "v": 0x56, "w": 0x57}

user32.OpenClipboard.argtypes = [wintypes.HWND]
user32.OpenClipboard.restype = wintypes.BOOL
user32.CloseClipboard.restype = wintypes.BOOL
user32.EmptyClipboard.restype = wintypes.BOOL
user32.GetClipboardData.argtypes = [wintypes.UINT]
user32.GetClipboardData.restype = wintypes.HANDLE
user32.SetClipboardData.argtypes = [wintypes.UINT, wintypes.HANDLE]
user32.SetClipboardData.restype = wintypes.HANDLE
user32.GetForegroundWindow.restype = wintypes.HWND
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.SetForegroundWindow.restype = wintypes.BOOL
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.keybd_event.argtypes = [wintypes.BYTE, wintypes.BYTE, wintypes.DWORD, ctypes.c_size_t]
kernel32.GlobalAlloc.argtypes = [wintypes.UINT, ctypes.c_size_t]
kernel32.GlobalAlloc.restype = wintypes.HGLOBAL
kernel32.GlobalLock.argtypes = [wintypes.HGLOBAL]
kernel32.GlobalLock.restype = wintypes.LPVOID
kernel32.GlobalUnlock.argtypes = [wintypes.HGLOBAL]
kernel32.GlobalFree.argtypes = [wintypes.HGLOBAL]
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                ctypes.POINTER(wintypes.DWORD)]


# ---- abrir coisas ----

def open_uri(uri):
    """URI/URL ou protocolo registrado (whatsapp:, ms-settings:, https://...)."""
    os.startfile(uri)


def run(argv, timeout=10):
    """Roda um programa fixo, sem shell. Devolve (codigo, saida de erro)."""
    r = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, errors="replace")
    return r.returncode, ((r.stderr or "") + (r.stdout or "")).strip()


def spawn(argv):
    """Abre um programa de janela sem esperar ele fechar."""
    subprocess.Popen(argv, close_fds=True)


# ---- area de transferencia ----

def _open_clipboard():
    for _ in range(20):                 # outro programa pode estar segurando
        if user32.OpenClipboard(None):
            return
        time.sleep(0.05)
    raise RuntimeError("a area de transferencia do Windows esta ocupada")


def clip_get():
    _open_clipboard()
    try:
        h = user32.GetClipboardData(CF_UNICODETEXT)
        if not h:
            return None
        p = kernel32.GlobalLock(h)
        try:
            return ctypes.wstring_at(p) if p else None
        finally:
            kernel32.GlobalUnlock(h)
    finally:
        user32.CloseClipboard()


def clip_set(text):
    data = (text or "").encode("utf-16-le") + b"\x00\x00"
    h = kernel32.GlobalAlloc(GMEM_MOVEABLE, len(data))
    if not h:
        raise RuntimeError("sem memoria para a area de transferencia")
    p = kernel32.GlobalLock(h)
    ctypes.memmove(p, data, len(data))
    kernel32.GlobalUnlock(h)
    _open_clipboard()
    try:
        user32.EmptyClipboard()
        if not user32.SetClipboardData(CF_UNICODETEXT, h):
            kernel32.GlobalFree(h)
            raise RuntimeError("nao consegui escrever na area de transferencia")
        # depois do SetClipboardData o Windows e dono da memoria: nao liberar
    finally:
        user32.CloseClipboard()


# ---- teclado e janelas ----

def keys(*names):
    """Aperta as teclas na ordem e solta na ordem inversa (ex.: keys('ctrl', 'v'))."""
    codes = [VK[n] for n in names]
    for c in codes:
        user32.keybd_event(c, 0, 0, 0)
    for c in reversed(codes):
        user32.keybd_event(c, 0, KEYEVENTF_KEYUP, 0)
    time.sleep(0.05)


def _process_name(hwnd):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    hp = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
    if not hp:
        return ""
    try:
        buf = ctypes.create_unicode_buffer(520)
        n = wintypes.DWORD(len(buf))
        if kernel32.QueryFullProcessImageNameW(hp, 0, buf, ctypes.byref(n)):
            return os.path.basename(buf.value)
        return ""
    finally:
        kernel32.CloseHandle(hp)


def foreground():
    """(titulo, executavel) da janela que esta na frente agora."""
    hwnd = user32.GetForegroundWindow()
    if not hwnd:
        return "", ""
    buf = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(hwnd, buf, len(buf))
    return buf.value, _process_name(hwnd)


def bring_to_front(title):
    """Tenta trazer para a frente a janela com esse titulo exato."""
    hwnd = user32.FindWindowW(None, title)
    if not hwnd:
        return False
    return focus(hwnd)


user32.EnumWindows.argtypes = [ctypes.c_void_p, wintypes.LPARAM]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsIconic.argtypes = [wintypes.HWND]
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
_ENUM_CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
SW_RESTORE = 9
WM_CLOSE = 0x0010


def windows():
    """Janelas visiveis com titulo: lista de (hwnd, titulo, executavel)."""
    out = []

    def cb(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(512)
            user32.GetWindowTextW(hwnd, buf, len(buf))
            if buf.value.strip():
                out.append((hwnd, buf.value, _process_name(hwnd)))
        return True

    user32.EnumWindows(_ENUM_CB(cb), 0)
    return out


def focus(hwnd):
    """Traz a janela para a frente (restaura se estiver minimizada)."""
    if user32.IsIconic(hwnd):
        user32.ShowWindow(hwnd, SW_RESTORE)
    # o Windows so deixa trocar o foco logo depois de uma tecla: um ALT solto libera
    keys("alt")
    ok = bool(user32.SetForegroundWindow(hwnd))
    time.sleep(0.25)
    return ok


def foreground_hwnd():
    return user32.GetForegroundWindow()


def close_window(hwnd):
    """Pede para a janela fechar, como o X: o app pode perguntar se quer salvar."""
    user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
