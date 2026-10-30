package com.ferluht.fm1sampler;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.view.WindowManager;
import java.io.File;

/** The app: starts the backend (engine, FM-1 link, audio), shows the sampler and opens the system file picker when the UI asks for a file. */
public class SamplerActivity extends Activity {
    static final int PICK = 7;
    Backend backend;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setStatusBarColor(0xff14141a);
        getWindow().setNavigationBarColor(0xff14141a);
        backend = Backend.get(this);
        backend.onFileRequest = this::pickFile;
        backend.onMidiRequest = this::pickMidi;
        backend.onShare = this::share;
        backend.start();
        SamplerView v = new SamplerView(this, backend.engine, 6, 6);
        v.setFitsSystemWindows(true);
        setContentView(v);
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) backend.startMic();
        else requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, 1);
    }

    static final int PICK_MIDI = 8;
    void pickMidi() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"audio/midi", "audio/mid", "audio/x-midi", "application/x-midi", "application/octet-stream"});
        try { startActivityForResult(i, PICK_MIDI); } catch (Exception e) { Native.say(backend.engine, "NO FILE PICKER ON THIS PHONE"); }
    }

    /** An exported file (a .mid or a .wav in the data dir) goes to the share sheet through ExportProvider. */
    void share(String path) {
        File f = new File(path);
        String type = path.endsWith(".mid") ? "audio/midi" : "audio/wav";
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType(type);
        send.putExtra(Intent.EXTRA_STREAM, Uri.parse("content://" + ExportProvider.AUTHORITY + "/" + Uri.encode(f.getName()) + "?p=" + Uri.encode(path)));
        send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { startActivity(Intent.createChooser(send, "Share " + f.getName()).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)); } catch (Exception e) { Native.say(backend.engine, "NOTHING TO SHARE WITH"); }
    }

    @Override protected void onPause() {
        Native.autosave(backend.engine);   // the project is saved when the app goes away
        super.onPause();
    }

    void pickFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"audio/*", "application/ogg"});
        try { startActivityForResult(i, PICK); } catch (Exception e) { Native.say(backend.engine, "NO FILE PICKER ON THIS PHONE"); }
    }

    @Override protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code == PICK_MIDI && result == RESULT_OK && data != null && data.getData() != null) {
            final Uri midiUri = data.getData();
            new Thread(() -> {
                try (java.io.InputStream in = getContentResolver().openInputStream(midiUri); java.io.FileOutputStream out = new java.io.FileOutputStream(new File(getCacheDir(), "import.mid"))) {
                    byte[] b = new byte[8192]; int n; long total = 0;
                    while (in != null && (n = in.read(b)) > 0 && (total += n) < (4 << 20)) out.write(b, 0, n);
                } catch (Exception e) { Native.say(backend.engine, "CANNOT READ THIS FILE"); return; }
                Native.importMidi(backend.engine, new File(getCacheDir(), "import.mid").getAbsolutePath());
            }, "import-midi").start();
            return;
        }
        if (code != PICK || result != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        Native.say(backend.engine, "READING THE FILE...");
        new Thread(() -> {   // decoding can take a moment: off the UI thread
            try {
                File out = AudioImport.importTo(this, uri, getFilesDir());
                Native.importFile(backend.engine, out.getAbsolutePath());
            } catch (Exception e) {
                android.util.Log.w("fm1sampler", "import: " + e);
                Native.say(backend.engine, "CANNOT READ THIS FILE");
            }
        }, "import").start();
    }

    @Override public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        if (res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED) backend.startMic();
    }
}
