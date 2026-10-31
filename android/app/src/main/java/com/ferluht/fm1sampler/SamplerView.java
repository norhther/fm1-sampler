package com.ferluht.fm1sampler;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

/** Replays the draw ops built by the C++ PhoneUI (engine/ui/PhoneUI.cpp) and forwards touches to it. All layout lives in C++. */
public class SamplerView extends View {
    final long h;
    float d = 1;                         // layout unit -> px: the UI is laid out for a fixed height of 400 units, so it scales to any phone
    final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    final RectF r = new RectF();
    final Path path = new Path();
    final Typeface regular = Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL), bold = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD);
    final float topDp, bottomDp;

    public SamplerView(Context c, long engine, float topDp, float bottomDp) {
        super(c);
        h = engine; this.topDp = topDp; this.bottomDp = bottomDp;
        setBackgroundColor(0xff14141a);
        setContentDescription("FM-1 sampler");
        p.setAntiAlias(false);
    }

    static float f(int bits) { return Float.intBitsToFloat(bits); }

    @Override protected void onDraw(Canvas c) {
        long now = SystemClock.uptimeMillis();
        d = Math.min(getHeight() / 400f, getWidth() / 860f);   // tablets (4:3, 16:10): fixed 860 units wide, taller virtual height
        Object[] fr = Native.uiFrame(h, getWidth() / d, getHeight() / d, topDp, bottomDp, now);
        int[] a = (int[]) fr[0];
        String[] texts = ((String) fr[1]).split("\u0001", -1);
        int saves = 0;
        for (int i = 0; i + 8 < a.length; i += 9) {
            final int type = a[i];
            final float f0 = f(a[i + 1]) * d, f1 = f(a[i + 2]) * d, f2 = f(a[i + 3]) * d, f3 = f(a[i + 4]) * d, f4 = f(a[i + 5]) * d, f5 = f(a[i + 6]) * d;
            final int color = a[i + 7];
            p.setColor(color);
            switch (type) {
            case 0:   // RRECT x y w h r
                p.setStyle(Paint.Style.FILL); r.set(f0, f1, f0 + f2, f1 + f3);
                if (f4 > 0) c.drawRoundRect(r, f4, f4, p); else c.drawRect(r, p);
                break;
            case 1:   // STROKE x y w h r width(dp)
                p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(f5); r.set(f0, f1, f0 + f2, f1 + f3);
                c.drawRoundRect(r, f4, f4, p); p.setStyle(Paint.Style.FILL);
                break;
            case 2: { // TEXT x baseline size align bold   (size is dp: f2 already scaled)
                p.setStyle(Paint.Style.FILL); p.setTextSize(f2);
                p.setTypeface(f(a[i + 5]) != 0 ? bold : regular);
                int al = (int) f(a[i + 4]);
                p.setTextAlign(al == 1 ? Paint.Align.CENTER : al == 2 ? Paint.Align.RIGHT : Paint.Align.LEFT);
                int ti = a[i + 8];
                if (ti >= 0 && ti < texts.length) c.drawText(texts[ti], f0, f1, p);
                break;
            }
            case 3:   // LINE
                p.setStrokeWidth(f4); p.setStrokeCap(Paint.Cap.ROUND); c.drawLine(f0, f1, f2, f3, p);
                break;
            case 4:   // CIRCLE
                p.setStyle(Paint.Style.FILL); c.drawCircle(f0, f1, f2, p);
                break;
            case 5:   // TRI
                p.setStyle(Paint.Style.FILL); path.reset(); path.moveTo(f0, f1); path.lineTo(f2, f3); path.lineTo(f4, f5); path.close(); c.drawPath(path, p);
                break;
            case 6:   // CLIP
                c.save(); saves++; c.clipRect(f0, f1, f0 + f2, f1 + f3);
                break;
            case 7:
                if (saves > 0) { c.restore(); saves--; }
                break;
            }
        }
        while (saves-- > 0) c.restore();
        String say = Native.uiAnnounce(h);   // toasts are read out by TalkBack
        if (say != null && !say.isEmpty()) announceForAccessibility(say);
        postInvalidateOnAnimation();
    }

    private boolean pinching;   // two fingers on the piano roll: a pinch zooms it, the single finger events are ignored meanwhile

    private void touch(MotionEvent e, int index, int action) {
        Native.uiTouch(h, e.getPointerId(index), action, e.getX(index) / d, e.getY(index) / d, SystemClock.uptimeMillis());
        if (action == 0 && Native.uiHaptic(h)) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);   // every control ticks under the finger
    }

    /** Every finger is its own touch (the pads play together); two fingers on the piano roll are a pinch instead. */
    @Override public boolean onTouchEvent(MotionEvent e) {
        final int act = e.getActionMasked(), idx = e.getActionIndex();
        switch (act) {
        case MotionEvent.ACTION_DOWN:
            pinching = false; touch(e, 0, 0); return true;
        case MotionEvent.ACTION_POINTER_DOWN:
            if (pinching) return true;
            if (e.getPointerCount() == 2 && Native.uiPinch(h, 0, e.getX(0) / d, e.getY(0) / d, e.getX(1) / d, e.getY(1) / d)) { pinching = true; return true; }
            touch(e, idx, 0); return true;
        case MotionEvent.ACTION_MOVE:
            if (pinching) { if (e.getPointerCount() >= 2) Native.uiPinch(h, 1, e.getX(0) / d, e.getY(0) / d, e.getX(1) / d, e.getY(1) / d); }
            else for (int i = 0; i < e.getPointerCount(); i++) touch(e, i, 1);
            return true;
        case MotionEvent.ACTION_POINTER_UP:
            if (pinching) { if (e.getPointerCount() == 2) Native.uiPinch(h, 2, 0, 0, 0, 0); } else touch(e, idx, 2);
            return true;
        case MotionEvent.ACTION_UP:
            if (pinching) { Native.uiPinch(h, 2, 0, 0, 0, 0); pinching = false; } else touch(e, idx, 2);
            return true;
        case MotionEvent.ACTION_CANCEL:   // the system took the touch away: nothing may stay pressed
            if (pinching) { Native.uiPinch(h, 2, 0, 0, 0, 0); pinching = false; }
            Native.uiCancel(h); return true;
        }
        return super.onTouchEvent(e);
    }
}
