package com.ferluht.fm1sampler;

import android.content.Context;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

/** Owns the native engine and the link to the FM-1: finds it on USB (MIDI + audio), routes the audio to it, runs the 30 Hz surface tick. */
public class Backend {
    static final String TAG = "fm1sampler";
    public static Backend instance;
    public final long engine;
    final Context ctx;
    final Handler main = new Handler(Looper.getMainLooper());
    final MidiManager midi;
    final AudioManager am;
    MidiDevice device;
    volatile MidiInputPort toFm1;
    MidiOutputPort fromFm1;
    volatile boolean running;
    Thread ticker;
    boolean audioStarted, micStarted, micWanted;
    int usbAudioId = 0;
    /** Set by the activity: opens the system file picker (the phone UI asked for LOAD FILE FROM DISK). */
    public volatile Runnable onFileRequest;
    /** A MIDI file was asked for (PROJECT sheet), and a file is ready for the share sheet. */
    public volatile Runnable onMidiRequest;
    public volatile java.util.function.Consumer<String> onShare;

    public static synchronized Backend get(Context c) {
        if (instance == null) instance = new Backend(c.getApplicationContext());
        return instance;
    }

    Backend(Context c) {
        ctx = c;
        java.io.File samples = new java.io.File(c.getFilesDir(), "samples");   // the bundled sample library, copied out again after every update: the engine reads plain files
        int version = 0;
        try { version = c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionCode; } catch (Exception e) { android.util.Log.w("fm1sampler", "version: " + e); }
        java.io.File marker = new java.io.File(samples, ".version");
        if (!marker.exists() || !readText(marker).equals(String.valueOf(version))) {   // first start, or an update that may bring new instruments
            try { copyAssets(c, "samples", samples); try (java.io.FileWriter w = new java.io.FileWriter(marker)) { w.write(String.valueOf(version)); } } catch (java.io.IOException e) { android.util.Log.w("fm1sampler", "samples: " + e); }
        }
        engine = Native.create(c.getFilesDir().getAbsolutePath(), samples.getAbsolutePath());
        midi = (MidiManager) c.getSystemService(Context.MIDI_SERVICE);
        am = (AudioManager) c.getSystemService(Context.AUDIO_SERVICE);
        Native.restoreLast(engine);   // back to where the app was left (the engine ignores it once if the last restore never finished)
    }

    public synchronized void start() {
        if (running) return;
        running = true;
        startAudio();
        if (midi != null) {
            midi.registerDeviceCallback(new MidiManager.DeviceCallback() {
                @Override public void onDeviceAdded(MidiDeviceInfo i) { connectMidi(); }
                @Override public void onDeviceRemoved(MidiDeviceInfo i) { disconnectMidi(); connectMidi(); }
            }, main);
            connectMidi();
        }
        am.registerAudioDeviceCallback(new AudioDeviceCallback() {
            @Override public void onAudioDevicesAdded(AudioDeviceInfo[] a) { restartAudioIfUsbChanged(); if (micWanted && !micStarted) startMic(); }
            @Override public void onAudioDevicesRemoved(AudioDeviceInfo[] a) { restartAudioIfUsbChanged(); }
        }, main);
        ticker = new Thread(() -> {
            long t0 = SystemClock.uptimeMillis();
            while (running) {
                byte[] out = Native.tick(engine, SystemClock.uptimeMillis() - t0);
                send(out);
                Native.uiLink(engine, toFm1 != null, usbAudioId != 0);
                if (Native.wantFile(engine)) { Runnable r = onFileRequest; if (r != null) main.post(r); }
                if (Native.wantMidi(engine)) { Runnable r = onMidiRequest; if (r != null) main.post(r); }
                String share = Native.sharePath(engine);
                if (share != null && !share.isEmpty()) { java.util.function.Consumer<String> c = onShare; if (c != null) main.post(() -> c.accept(share)); }
                try { Thread.sleep(16); } catch (InterruptedException ignored) { }
            }
        }, "fm1-surface");
        ticker.start();
    }

    public synchronized void stop() {
        running = false;
        try { if (ticker != null) ticker.join(300); } catch (InterruptedException ignored) { }
        Native.stopAudio(engine);
        audioStarted = micStarted = false;
        disconnectMidi();
    }

    int findUsbAudio() {
        int id = 0;
        for (AudioDeviceInfo d : am.getDevices(AudioManager.GET_DEVICES_OUTPUTS))
            if (d.getType() == AudioDeviceInfo.TYPE_USB_DEVICE || d.getType() == AudioDeviceInfo.TYPE_USB_HEADSET) id = d.getId();
        return id;
    }
    void startAudio() {
        usbAudioId = findUsbAudio();
        int r = Native.startAudio(engine, usbAudioId);
        audioStarted = r == 0;
        Log.i(TAG, "audio start " + r + " usb device " + usbAudioId);
    }
    void restartAudioIfUsbChanged() {
        if (!running) return;
        int id = findUsbAudio();
        if (id != usbAudioId) { Log.i(TAG, "usb audio device changed " + usbAudioId + " -> " + id); startAudio(); }
    }
    /** Call after RECORD_AUDIO was granted. The phone's own microphone, never the USB device. */
    public synchronized void startMic() {
        if (micStarted) return;
        int mic = 0;
        for (AudioDeviceInfo d : am.getDevices(AudioManager.GET_DEVICES_INPUTS)) if (d.getType() == AudioDeviceInfo.TYPE_BUILTIN_MIC) mic = d.getId();
        micWanted = true;
        micStarted = Native.startInput(engine, mic) == 0;
        Log.i(TAG, "mic start " + micStarted + " device " + mic);
    }

    // ---- MIDI ----
    synchronized void connectMidi() {
        if (toFm1 != null || midi == null) return;
        MidiDeviceInfo pick = null;
        for (MidiDeviceInfo i : midi.getDevices()) {
            String n = String.valueOf(i.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME)).toLowerCase();
            if (n.contains("fm") || n.contains("m-vave") || n.contains("mvave")) pick = i;
        }
        if (pick == null) return;
        midi.openDevice(pick, d -> {
            if (d == null) return;
            synchronized (Backend.this) {
                device = d;
                toFm1 = d.openInputPort(0);
                fromFm1 = d.openOutputPort(0);
                if (fromFm1 != null) fromFm1.connect(new MidiReceiver() {
                    @Override public void onSend(byte[] data, int off, int count, long ts) { Native.midiIn(engine, data, off, count); }
                });
            }
            Log.i(TAG, "FM-1 MIDI connected");
        }, main);
    }
    synchronized void disconnectMidi() {
        try { if (fromFm1 != null) fromFm1.close(); } catch (Exception ignored) { }
        try { if (toFm1 != null) toFm1.close(); } catch (Exception ignored) { }
        try { if (device != null) device.close(); } catch (Exception ignored) { }
        fromFm1 = null; toFm1 = null; device = null;
    }
    void send(byte[] b) {
        MidiInputPort port = toFm1;
        if (port == null || b.length == 0) return;
        int s = 0;
        for (int i = 0; i < b.length; i++) if ((b[i] & 0xff) == 0xf7) {
            try { port.send(b, s, i - s + 1); } catch (Exception e) { Log.w(TAG, "send: " + e); return; }
            s = i + 1;
        }
    }

    static String readText(java.io.File f) {
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(f))) { String l = r.readLine(); return l == null ? "" : l.trim(); } catch (java.io.IOException e) { return ""; }
    }
    static void copyAssets(Context c, String from, java.io.File to) throws java.io.IOException {
        String[] list = c.getAssets().list(from);
        if (list == null || list.length == 0) {   // a file
            to.getParentFile().mkdirs();
            try (java.io.InputStream in = c.getAssets().open(from); java.io.OutputStream out = new java.io.FileOutputStream(to)) {
                byte[] b = new byte[65536]; int n; while ((n = in.read(b)) > 0) out.write(b, 0, n);
            }
            return;
        }
        to.mkdirs();
        for (String f : list) copyAssets(c, from + "/" + f, new java.io.File(to, f));
    }
}
