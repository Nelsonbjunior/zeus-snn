package br.com.zeus.robo;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentValues;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Insets;
import android.graphics.Matrix;
import android.media.ExifInterface;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore;
import android.os.Handler;
import android.os.Looper;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.text.InputType;
import android.util.Log;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import android.widget.FrameLayout;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * App do Zeus: abre a pagina que o robo serve (http://192.168.0.201) e entrega a ela o
 * reconhecimento de voz e a voz do proprio Android, pelo objeto JS "ZeusAndroid".
 * Assim nao precisa liberar o microfone em chrome://flags, e a voz e a do Google TTS.
 */
public class MainActivity extends Activity {

    // pela internet (Cloudflare Tunnel -> ponte do notebook): funciona em qualquer rede, sem cabo.
    // Em "trocar endereço" ainda da para usar o robo direto na rede de casa (http://192.168.0.201).
    static final String URL_PADRAO = "https://zeus.appdevtest.shop/";

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final AtomicInteger escuta = new AtomicInteger();   // id da escuta atual
    private SharedPreferences prefs;
    private WebView web;
    private SpeechRecognizer sr;
    private TextToSpeech tts;
    private volatile boolean ttsPronto;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);   // modo Alexa: tela acesa
        prefs = getSharedPreferences("zeus", MODE_PRIVATE);

        WebView.setWebContentsDebuggingEnabled(true);          // depuracao em chrome://inspect
        web = new WebView(this);
        web.setBackgroundColor(0xFF0B0B0B);
        // Android 15 desenha atras da barra de status e da de navegacao: a moldura afasta a
        // pagina delas (padding no proprio WebView nao move o conteudo)
        FrameLayout moldura = new FrameLayout(this);
        moldura.setBackgroundColor(0xFF0B0B0B);
        moldura.addView(web, new FrameLayout.LayoutParams(-1, -1));
        setContentView(moldura);
        moldura.setOnApplyWindowInsetsListener((v, ins) -> {
            Insets i = ins.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.ime());
            v.setPadding(i.left, i.top, i.right, i.bottom);
            return WindowInsets.CONSUMED;
        });
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setCacheMode(WebSettings.LOAD_NO_CACHE);              // firmware novo = pagina nova
        web.addJavascriptInterface(new Ponte(), "ZeusAndroid");
        web.setWebChromeClient(new WebChromeClient());
        web.setWebViewClient(new WebViewClient() {
            @Override
            public void onReceivedError(WebView v, WebResourceRequest r, WebResourceError e) {
                if (r.isForMainFrame()) mostrarErro(String.valueOf(e.getDescription()));
            }
        });

        tts = new TextToSpeech(this, st -> {
            if (st != TextToSpeech.SUCCESS) return;
            tts.setLanguage(new Locale("pt", "BR"));
            tts.setOnUtteranceProgressListener(new UtteranceProgressListener() {
                @Override public void onStart(String id) { }
                @Override public void onDone(String id) { js("__zeusTTS", id); }
                @Override public void onError(String id) { js("__zeusTTS", id); }
                @Override public void onStop(String id, boolean interrompida) { js("__zeusTTS", id); }
                // cada palavra que a voz comeca: a boca do Zeus abre no OLED
                @Override public void onRangeStart(String id, int ini, int fim, int quadro) { js("__zeusPalavra"); }
            });
            ttsPronto = true;
        });

        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, 1);
        }
        carregar();
    }

    private String endereco() {
        return prefs.getString("url", URL_PADRAO);
    }

    private void carregar() {
        web.loadUrl(endereco());
    }

    private void mostrarErro(String motivo) {
        String html = "<html><body style='background:#0b0b0b;color:#eee;font:16px system-ui;padding:24px'>"
                + "<h2>Não achei o Zeus</h2><p>Endereço: <b>" + endereco() + "</b><br>" + motivo + "</p>"
                + "<p>O robô está ligado e na mesma rede WiFi do celular?</p>"
                + "<p><button style='padding:14px;font-size:16px' onclick='ZeusAndroid.recarregar()'>tentar de novo</button> "
                + "<button style='padding:14px;font-size:16px' onclick='ZeusAndroid.trocarEndereco()'>trocar endereço</button></p>"
                + "</body></html>";
        web.loadDataWithBaseURL(null, html, "text/html", "utf-8", null);
    }

    private void dialogoEndereco() {
        EditText campo = new EditText(this);
        campo.setInputType(InputType.TYPE_TEXT_VARIATION_URI);
        campo.setText(endereco());
        new AlertDialog.Builder(this)
                .setTitle("Endereço do Zeus")
                .setView(campo)
                .setPositiveButton("salvar", (d, w) -> {
                    String u = campo.getText().toString().trim();
                    if (!u.startsWith("http")) u = "http://" + u;
                    prefs.edit().putString("url", u).apply();
                    carregar();
                })
                .setNegativeButton("cancelar", null)
                .show();
    }

    // chama window.fn(args...) na pagina, com os textos escapados
    private void js(String fn, Object... args) {
        StringBuilder sb = new StringBuilder("window.").append(fn).append("&&window.").append(fn).append('(');
        for (int i = 0; i < args.length; i++) {
            if (i > 0) sb.append(',');
            Object a = args[i];
            if (a == null) sb.append("null");
            else if (a instanceof Number) sb.append(a);
            else sb.append(JSONObject.quote(a.toString()));
        }
        sb.append(')');
        String code = sb.toString();
        ui.post(() -> web.evaluateJavascript(code, null));
    }

    // ---------------- ouvir: SpeechRecognizer do Android ----------------

    private void iniciarEscuta(int id) {
        if (id != escuta.get()) return;                        // ja pediram para parar
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            js("__zeusSR", id, "erro", "not-allowed");
            js("__zeusSR", id, "fim", null);
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, 1);
            return;
        }
        if (!SpeechRecognizer.isRecognitionAvailable(this)) {
            js("__zeusSR", id, "erro", "service-not-allowed");
            js("__zeusSR", id, "fim", null);
            return;
        }
        if (sr == null) sr = SpeechRecognizer.createSpeechRecognizer(this);
        else sr.cancel();
        sr.setRecognitionListener(new Ouvinte(id));
        Intent it = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        it.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        it.putExtra(RecognizerIntent.EXTRA_LANGUAGE, "pt-BR");
        it.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);
        // varias alternativas: muitas vezes a 1a vem "viu que horas" e a 2a "Zeus que horas"
        it.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 5);
        it.putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, getPackageName());
        it.putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, false);
        // O Google em portugues descarta "Zeus" no comeco da frase; a lista puxa o reconhecedor
        // para esse nome (Android 13+; quem nao suporta, ignora).
        ArrayList<String> nomes = new ArrayList<>();
        nomes.add("Zeus");
        it.putStringArrayListExtra(RecognizerIntent.EXTRA_BIASING_STRINGS, nomes);
        sr.startListening(it);
    }

    private final class Ouvinte implements RecognitionListener {
        private final int id;

        private String ultimoParcial = "";

        Ouvinte(int id) { this.id = id; }

        private boolean atual() { return id == escuta.get(); }

        private String texto(Bundle b) {
            ArrayList<String> r = b == null ? null : b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
            return r == null || r.isEmpty() ? "" : r.get(0);
        }

        @Override public void onPartialResults(Bundle b) {
            String t = texto(b);
            if (!t.isEmpty()) ultimoParcial = t;
            if (atual() && !t.isEmpty()) js("__zeusSR", id, "parcial", t);
        }

        // todas as alternativas, uma por linha (a melhor primeiro); a pagina escolhe a que tem o nome
        private String alternativas(Bundle b) {
            ArrayList<String> r = b == null ? null : b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
            if (r == null) return "";
            StringBuilder sb = new StringBuilder();
            for (String s : r) {
                if (s == null || s.trim().isEmpty()) continue;
                if (sb.length() > 0) sb.append('\n');
                sb.append(s.trim());
            }
            return sb.toString();
        }

        @Override public void onResults(Bundle b) {
            String t = alternativas(b);
            // No S21 o resultado final chega VAZIO mesmo com a frase reconhecida nos parciais:
            // sem isso a pagina nunca recebia o pedido. Fica com o ultimo parcial.
            if (t.isEmpty()) t = ultimoParcial;
            Log.i("ZeusApp", "onResults id=" + id + " atual=" + escuta.get() + " texto=" + t);
            if (!atual()) return;
            if (!t.isEmpty()) js("__zeusSR", id, "final", t);
            js("__zeusSR", id, "fim", null);
        }

        @Override public void onError(int code) {
            Log.i("ZeusApp", "onError id=" + id + " atual=" + escuta.get() + " code=" + code);
            if (!atual()) return;
            // o mesmo defeito as vezes vira erro "sem fala" depois de parciais validos
            if (!ultimoParcial.isEmpty() && (code == SpeechRecognizer.ERROR_NO_MATCH
                    || code == SpeechRecognizer.ERROR_SPEECH_TIMEOUT)) {
                js("__zeusSR", id, "final", ultimoParcial);
                js("__zeusSR", id, "fim", null);
                return;
            }
            String e;
            switch (code) {
                case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS: e = "not-allowed"; break;
                case SpeechRecognizer.ERROR_NO_MATCH:
                case SpeechRecognizer.ERROR_SPEECH_TIMEOUT: e = "no-speech"; break;
                case SpeechRecognizer.ERROR_RECOGNIZER_BUSY: e = "busy"; break;
                default: e = "erro-" + code;
            }
            js("__zeusSR", id, "erro", e);
            js("__zeusSR", id, "fim", null);
        }

        @Override public void onReadyForSpeech(Bundle b) { }
        @Override public void onBeginningOfSpeech() { }
        @Override public void onRmsChanged(float db) { }
        @Override public void onBufferReceived(byte[] buf) { }
        @Override public void onEndOfSpeech() { }
        @Override public void onEvent(int tipo, Bundle b) { }
    }

    // ---------------- objeto JS "ZeusAndroid" ----------------

    private final class Ponte {
        @JavascriptInterface public int ouvir() {
            int id = escuta.incrementAndGet();
            ui.post(() -> iniciarEscuta(id));
            return id;
        }

        @JavascriptInterface public void pararOuvir() {
            escuta.incrementAndGet();                           // eventos da escuta velha somem
            ui.post(() -> { if (sr != null) sr.cancel(); });
        }

        @JavascriptInterface public void falar(String id, String texto) {
            ui.post(() -> {
                if (!ttsPronto) { js("__zeusTTS", id); return; }
                tts.speak(texto, TextToSpeech.QUEUE_FLUSH, new Bundle(), id);
            });
        }

        @JavascriptInterface public void calar() {
            ui.post(() -> { if (tts != null) tts.stop(); });
        }

        @JavascriptInterface public void trocarEndereco() { ui.post(MainActivity.this::dialogoEndereco); }

        @JavascriptInterface public void recarregar() { ui.post(MainActivity.this::carregar); }

        @JavascriptInterface public String versao() { return "1.5.0"; }

        // nome que o dono deu ao celular (Configuracoes > Sobre o telefone), para o xadrez
        @JavascriptInterface public String nomeAparelho() {
            String n = null;
            try { n = android.provider.Settings.Global.getString(getContentResolver(), "device_name"); }
            catch (Exception ignored) { }
            if (n == null || n.trim().isEmpty()) {
                try { n = android.provider.Settings.Secure.getString(getContentResolver(), "bluetooth_name"); }
                catch (Exception ignored) { }
            }
            if (n == null || n.trim().isEmpty()) n = android.os.Build.MODEL;
            return n == null ? "" : n.trim();
        }

        // abre a camera do celular; a foto vai para a ponte (url + "/foto") e a pagina recebe
        // window.__zeusFoto("ok"|"cancelada"|"erro", detalhe)
        @JavascriptInterface public void tirarFoto(String urlPonte, String token) {
            ui.post(() -> abrirCamera(urlPonte, token));
        }

        // depois de publicada no Instagram: tira da galeria a foto que este app gravou
        @JavascriptInterface public void apagarFoto() {
            ui.post(() -> {
                Uri u = fotoUri;
                fotoUri = null;
                if (u == null) return;
                try {
                    getContentResolver().delete(u, null, null);
                } catch (Exception e) {
                    Log.w("ZeusApp", "apagar foto", e);
                }
            });
        }
    }

    // ---------------- foto: camera do celular -> ponte do notebook ----------------

    private static final int REQ_FOTO = 7;
    private static final int LADO_MAX = 1440;           // o Instagram nao usa mais que isso
    private Uri fotoUri;
    private String fotoPonte, fotoToken;

    private void abrirCamera(String urlPonte, String token) {
        fotoPonte = urlPonte;
        fotoToken = token;
        ContentValues v = new ContentValues();
        v.put(MediaStore.Images.Media.DISPLAY_NAME, "zeus_" + System.currentTimeMillis() + ".jpg");
        v.put(MediaStore.Images.Media.MIME_TYPE, "image/jpeg");
        v.put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/Zeus");    // fica na galeria tambem
        fotoUri = getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, v);
        if (fotoUri == null) { js("__zeusFoto", "erro", "não consegui criar o arquivo da foto"); return; }
        Intent it = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
        it.putExtra(MediaStore.EXTRA_OUTPUT, fotoUri);
        it.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try {
            startActivityForResult(it, REQ_FOTO);
        } catch (Exception e) {
            getContentResolver().delete(fotoUri, null, null);
            js("__zeusFoto", "erro", "não achei o app de câmera do celular");
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_FOTO) return;
        Uri uri = fotoUri;
        if (res != RESULT_OK) {
            if (uri != null) getContentResolver().delete(uri, null, null);
            js("__zeusFoto", "cancelada", "");
            return;
        }
        new Thread(() -> {
            try {
                byte[] jpg = reduzir(uri);
                String nome = enviarFoto(jpg);
                js("__zeusFoto", "ok", nome);
            } catch (Exception e) {
                Log.w("ZeusApp", "foto", e);
                js("__zeusFoto", "erro", "a foto não chegou no notebook: " + e.getMessage());
            }
        }).start();
    }

    // reduz para LADO_MAX e endireita pela orientacao EXIF (a camera grava "deitada")
    private byte[] reduzir(Uri uri) throws Exception {
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inJustDecodeBounds = true;
        try (InputStream in = getContentResolver().openInputStream(uri)) { BitmapFactory.decodeStream(in, null, o); }
        int amostra = 1;
        while (Math.max(o.outWidth, o.outHeight) / (amostra * 2) >= LADO_MAX) amostra *= 2;
        BitmapFactory.Options o2 = new BitmapFactory.Options();
        o2.inSampleSize = amostra;
        Bitmap bmp;
        try (InputStream in = getContentResolver().openInputStream(uri)) { bmp = BitmapFactory.decodeStream(in, null, o2); }
        if (bmp == null) throw new Exception("não consegui ler a foto");
        int giro = 0;
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            int ori = new ExifInterface(in).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL);
            giro = ori == ExifInterface.ORIENTATION_ROTATE_90 ? 90 : ori == ExifInterface.ORIENTATION_ROTATE_180 ? 180
                    : ori == ExifInterface.ORIENTATION_ROTATE_270 ? 270 : 0;
        }
        float esc = Math.min(1f, (float) LADO_MAX / Math.max(bmp.getWidth(), bmp.getHeight()));
        Matrix m = new Matrix();
        m.postScale(esc, esc);
        m.postRotate(giro);
        Bitmap fim = Bitmap.createBitmap(bmp, 0, 0, bmp.getWidth(), bmp.getHeight(), m, true);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        fim.compress(Bitmap.CompressFormat.JPEG, 88, out);
        return out.toByteArray();
    }

    private String enviarFoto(byte[] jpg) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(fotoPonte + "/foto").openConnection();
        c.setRequestMethod("POST");
        c.setDoOutput(true);
        c.setConnectTimeout(8000);
        c.setReadTimeout(30000);
        c.setRequestProperty("Content-Type", "image/jpeg");
        c.setRequestProperty("X-Zeus-Token", fotoToken);
        c.setFixedLengthStreamingMode(jpg.length);
        try (OutputStream os = c.getOutputStream()) { os.write(jpg); }
        int code = c.getResponseCode();
        InputStream in = code < 400 ? c.getInputStream() : c.getErrorStream();
        String corpo = new String(in.readAllBytes(), StandardCharsets.UTF_8);
        JSONObject j = new JSONObject(corpo);
        if (code != 200) throw new Exception(j.optString("erro", "HTTP " + code));
        return j.optString("foto");
    }

    @Override
    protected void onDestroy() {
        if (sr != null) sr.destroy();
        if (tts != null) tts.shutdown();
        web.destroy();
        super.onDestroy();
    }
}
