package com.hashem.wifiscanner;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.SweepGradient;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * Animated radar. Every network is drawn as a blip: stronger signals sit closer to the center, and
 * blips light up as the sweep passes over them.
 */
public class RadarView extends View {

    /** Fraction of the radius kept clear in the middle for the network count. */
    private static final float CENTER_FRACTION = 0.40f;

    public static final class Blip {
        final float angleDeg;
        final float distance;
        final int color;
        final boolean highlight;

        public Blip(float angleDeg, float distance, int color, boolean highlight) {
            this.angleDeg = angleDeg;
            this.distance = distance;
            this.color = color;
            this.highlight = highlight;
        }
    }

    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint sweepPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint edgePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint blipPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint haloPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint highlightPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint centerPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint centerRingPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Matrix matrix = new Matrix();
    private final List<Blip> blips = new ArrayList<>();
    private final float density;
    private final int accent;

    private SweepGradient gradient;
    private float sweepAngle = -90f;
    private long lastFrame;
    private boolean running;
    private boolean scanning;

    public RadarView(Context context) {
        this(context, null);
    }

    public RadarView(Context context, AttributeSet attrs) {
        super(context, attrs);
        density = getResources().getDisplayMetrics().density;
        accent = context.getColor(R.color.accent);

        fillPaint.setColor(context.getColor(R.color.radar_fill));

        ringPaint.setStyle(Paint.Style.STROKE);
        ringPaint.setStrokeWidth(dp(1));
        ringPaint.setColor(context.getColor(R.color.radar_ring));

        edgePaint.setStyle(Paint.Style.STROKE);
        edgePaint.setStrokeWidth(dp(2));
        edgePaint.setStrokeCap(Paint.Cap.ROUND);
        edgePaint.setColor(accent);

        highlightPaint.setStyle(Paint.Style.STROKE);
        highlightPaint.setStrokeWidth(dp(1.5f));
        highlightPaint.setColor(Color.WHITE);

        centerPaint.setColor(context.getColor(R.color.bg));

        centerRingPaint.setStyle(Paint.Style.STROKE);
        centerRingPaint.setStrokeWidth(dp(1.5f));
        centerRingPaint.setColor(withAlpha(accent, 90));
    }

    public void setBlips(List<Blip> newBlips) {
        blips.clear();
        if (newBlips != null) blips.addAll(newBlips);
        invalidate();
    }

    /** Animate while the screen is visible. */
    public void setRunning(boolean run) {
        if (running == run) return;
        running = run;
        lastFrame = 0;
        invalidate();
    }

    /** Spin faster while a scan is in progress. */
    public void setScanning(boolean value) {
        scanning = value;
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        gradient = new SweepGradient(w / 2f, h / 2f,
                new int[]{withAlpha(accent, 0), withAlpha(accent, 0), withAlpha(accent, 70),
                        withAlpha(accent, 170)},
                new float[]{0f, 0.62f, 0.96f, 1f});
        sweepPaint.setShader(gradient);
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        running = false;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float cx = getWidth() / 2f;
        float cy = getHeight() / 2f;
        float radius = Math.min(cx, cy) - dp(4);
        if (radius <= 0) return;

        long now = SystemClock.uptimeMillis();
        if (running) {
            if (lastFrame != 0) {
                float degPerMs = scanning ? 0.32f : 0.12f;
                sweepAngle = (sweepAngle + (now - lastFrame) * degPerMs) % 360f;
            }
            lastFrame = now;
        }

        canvas.drawCircle(cx, cy, radius, fillPaint);
        for (int i = 1; i <= 4; i++) {
            canvas.drawCircle(cx, cy, radius * i / 4f, ringPaint);
        }
        canvas.drawLine(cx - radius, cy, cx + radius, cy, ringPaint);
        canvas.drawLine(cx, cy - radius, cx, cy + radius, ringPaint);

        if (gradient != null) {
            matrix.setRotate(sweepAngle, cx, cy);
            gradient.setLocalMatrix(matrix);
            canvas.drawCircle(cx, cy, radius, sweepPaint);
        }
        double rad = Math.toRadians(sweepAngle);
        canvas.drawLine(cx, cy, cx + (float) (Math.cos(rad) * radius),
                cy + (float) (Math.sin(rad) * radius), edgePaint);

        for (Blip b : blips) {
            double a = Math.toRadians(b.angleDeg);
            float r = radius * b.distance;
            float x = cx + (float) (Math.cos(a) * r);
            float y = cy + (float) (Math.sin(a) * r);
            float since = ((sweepAngle - b.angleDeg) % 360f + 360f) % 360f;
            float glow = 1f - since / 360f;

            haloPaint.setColor(withAlpha(b.color, (int) (25 + 70 * glow)));
            canvas.drawCircle(x, y, dp(b.highlight ? 13 : 10), haloPaint);
            blipPaint.setColor(withAlpha(b.color, (int) (140 + 115 * glow)));
            canvas.drawCircle(x, y, dp(b.highlight ? 5.5f : 4f), blipPaint);
            if (b.highlight) {
                canvas.drawCircle(x, y, dp(8), highlightPaint);
            }
        }

        float centerRadius = radius * CENTER_FRACTION;
        canvas.drawCircle(cx, cy, centerRadius, centerPaint);
        canvas.drawCircle(cx, cy, centerRadius, centerRingPaint);

        if (running) postInvalidateOnAnimation();
    }

    /** Places a network on the radar. Angle is stable per access point; distance follows signal. */
    public static Blip blipFor(String bssid, String fallbackKey, int levelDbm, int color,
                               boolean highlight) {
        String key = bssid != null ? bssid : (fallbackKey != null ? fallbackKey : "");
        long h = (key.hashCode() * 2654435761L) & 0xffffffffL;
        float angle = (h % 3600) / 10f;
        float t = (Math.min(Math.max(-levelDbm, 35), 95) - 35) / 60f;
        float distance = CENTER_FRACTION + 0.08f + t * (0.95f - CENTER_FRACTION - 0.08f);
        return new Blip(angle, distance, color, highlight);
    }

    private float dp(float v) {
        return v * density;
    }

    private static int withAlpha(int color, int alpha) {
        return (color & 0x00FFFFFF) | (Math.max(0, Math.min(255, alpha)) << 24);
    }
}
