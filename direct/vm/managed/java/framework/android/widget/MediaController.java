package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

public class MediaController extends FrameLayout {
    public interface MediaPlayerControl {
        void start();

        void pause();

        int getDuration();

        int getCurrentPosition();

        void seekTo(int pos);

        boolean isPlaying();

        int getBufferPercentage();

        boolean canPause();

        boolean canSeekBackward();

        boolean canSeekForward();

        int getAudioSessionId();
    }

    private MediaPlayerControl mPlayer;
    private boolean mShowing;

    public MediaController(Context context) {
        super(context);
    }

    public MediaController(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public MediaController(Context context, boolean useFastForward) {
        super(context);
    }

    public void setMediaPlayer(MediaPlayerControl player) {
        mPlayer = player;
    }

    public void setAnchorView(View view) {}

    public void show() {
        mShowing = true;
    }

    public void show(int timeout) {
        mShowing = true;
    }

    public boolean isShowing() {
        return mShowing;
    }

    public void hide() {
        mShowing = false;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        return true;
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        return super.onKeyDown(keyCode, event);
    }
}
