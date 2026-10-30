package com.ferluht.fm1sampler;

/** JNI bindings of native-lib.cpp (Session + SamplerApp + SamplerUI + Oboe). */
public final class Native {
    static { System.loadLibrary("fm1sampler"); }
    public static native long create(String dataDir, String sampleDir);
    public static native void destroy(long h);
    public static native int startAudio(long h, int deviceId);
    public static native int startInput(long h, int deviceId);
    public static native void stopAudio(long h);
    public static native int xruns(long h);
    public static native void midiIn(long h, byte[] data, int off, int n);
    public static native byte[] tick(long h, long nowMs);
    public static native boolean wantFile(long h);
    public static native void importFile(long h, String path);
    public static native void say(long h, String message);
    public static native void uiLink(long h, boolean midi, boolean usbAudio);
    public static native Object[] uiFrame(long h, float w, float hh, float top, float bottom, long nowMs);
    public static native void uiTouch(long h, int action, float x, float y, long nowMs);
    public static native void uiPinch(long h, int phase /* 0 start, 1 move, 2 end */, float x1, float y1, float x2, float y2);
    private Native() { }
}
