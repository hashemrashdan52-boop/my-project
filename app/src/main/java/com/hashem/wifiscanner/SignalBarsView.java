package com.hashem.wifiscanner;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

/** Four ascending bars that show signal strength. */
public class SignalBarsView extends View {

    private static final int BAR_COUNT = 4;

    private final Paint onPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint offPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private int filled;

    public SignalBarsView(Context context) {
        this(context, null);
    }

    public SignalBarsView(Context context, AttributeSet attrs) {
        super(context, attrs);
        offPaint.setColor(context.getColor(R.color.bars_off));
        onPaint.setColor(context.getColor(R.color.sig_excellent));
    }

    public void setSignal(int bars, int color) {
        if (filled == bars && onPaint.getColor() == color) return;
        filled = bars;
        onPaint.setColor(color);
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float left0 = getPaddingLeft();
        float top0 = getPaddingTop();
        float w = getWidth() - getPaddingLeft() - getPaddingRight();
        float h = getHeight() - getPaddingTop() - getPaddingBottom();
        if (w <= 0 || h <= 0) return;

        float gap = w * 0.12f;
        float barWidth = (w - gap * (BAR_COUNT - 1)) / BAR_COUNT;
        float radius = barWidth * 0.35f;
        for (int i = 0; i < BAR_COUNT; i++) {
            float barHeight = h * (i + 1) / BAR_COUNT;
            float left = left0 + i * (barWidth + gap);
            rect.set(left, top0 + h - barHeight, left + barWidth, top0 + h);
            canvas.drawRoundRect(rect, radius, radius, i < filled ? onPaint : offPaint);
        }
    }
}
