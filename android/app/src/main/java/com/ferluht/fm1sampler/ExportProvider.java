package com.ferluht.fm1sampler;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileNotFoundException;

/** Hands the exported files (MIDI, the tape mix) of the data dir to the share sheet. Only files inside the app's own exports folder are served. */
public class ExportProvider extends ContentProvider {
    public static final String AUTHORITY = "com.ferluht.fm1sampler.export";

    private File resolve(Uri uri) throws FileNotFoundException {
        String p = uri.getQueryParameter("p");
        if (p == null) throw new FileNotFoundException("no file");
        try {
            File f = new File(p).getCanonicalFile(), root = new File(getContext().getFilesDir(), "exports").getCanonicalFile();
            if (!f.getPath().startsWith(root.getPath() + File.separator) || !f.isFile()) throw new FileNotFoundException("not an export");
            return f;
        } catch (java.io.IOException e) { throw new FileNotFoundException("bad path"); }
    }
    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException { return ParcelFileDescriptor.open(resolve(uri), ParcelFileDescriptor.MODE_READ_ONLY); }
    @Override public Cursor query(Uri uri, String[] projection, String sel, String[] args, String order) {
        try {
            File f = resolve(uri);
            MatrixCursor c = new MatrixCursor(new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
            c.addRow(new Object[]{f.getName(), f.length()});
            return c;
        } catch (FileNotFoundException e) { return null; }
    }
    @Override public String getType(Uri uri) { String p = uri.getQueryParameter("p"); return p != null && p.endsWith(".mid") ? "audio/midi" : "audio/wav"; }
    @Override public boolean onCreate() { return true; }
    @Override public Uri insert(Uri uri, ContentValues v) { return null; }
    @Override public int delete(Uri uri, String s, String[] a) { return 0; }
    @Override public int update(Uri uri, ContentValues v, String s, String[] a) { return 0; }
}
