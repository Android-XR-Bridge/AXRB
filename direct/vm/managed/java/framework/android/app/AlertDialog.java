package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.view.View;

public class AlertDialog extends Dialog {
    private CharSequence mMessage;
    private CharSequence mTitle;
    private final CharSequence[] mButtonText = new CharSequence[3];
    private final DialogInterface.OnClickListener[] mButtonListener = new DialogInterface.OnClickListener[3];

    protected AlertDialog(Context context) {
        super(context);
    }

    protected AlertDialog(Context context, int themeResId) {
        super(context, themeResId);
    }

    public void setMessage(CharSequence message) {
        mMessage = message;
    }

    @Override
    public void setTitle(CharSequence title) {
        mTitle = title;
        super.setTitle(title);
    }

    public void setButton(int whichButton, CharSequence text, DialogInterface.OnClickListener listener) {
        int i = -whichButton - 1;
        if (i < 0 || i > 2) throw new IllegalArgumentException("Button does not exist");
        mButtonText[i] = text;
        mButtonListener[i] = listener;
    }

    @Override
    public void show() {
        android.util.Log.i("AlertDialog", "\"" + mTitle + "\": " + mMessage);
        super.show();
    }

    public static class Builder {
        private final Context mContext;
        private CharSequence mTitle;
        private CharSequence mMessage;
        private boolean mCancelable = true;
        private DialogInterface.OnCancelListener mOnCancel;
        private DialogInterface.OnDismissListener mOnDismiss;
        private final CharSequence[] mText = new CharSequence[3];
        private final DialogInterface.OnClickListener[] mListeners = new DialogInterface.OnClickListener[3];
        private View mView;

        public Builder(Context context) {
            mContext = context;
        }

        public Builder(Context context, int themeResId) {
            mContext = context;
        }

        public Context getContext() {
            return mContext;
        }

        public Builder setTitle(CharSequence title) {
            mTitle = title;
            return this;
        }

        public Builder setTitle(int titleId) {
            return setTitle(mContext.getText(titleId));
        }

        public Builder setMessage(CharSequence message) {
            mMessage = message;
            return this;
        }

        public Builder setMessage(int messageId) {
            return setMessage(mContext.getText(messageId));
        }

        public Builder setPositiveButton(CharSequence text, DialogInterface.OnClickListener listener) {
            mText[0] = text;
            mListeners[0] = listener;
            return this;
        }

        public Builder setPositiveButton(int textId, DialogInterface.OnClickListener listener) {
            return setPositiveButton(mContext.getText(textId), listener);
        }

        public Builder setNegativeButton(CharSequence text, DialogInterface.OnClickListener listener) {
            mText[1] = text;
            mListeners[1] = listener;
            return this;
        }

        public Builder setNegativeButton(int textId, DialogInterface.OnClickListener listener) {
            return setNegativeButton(mContext.getText(textId), listener);
        }

        public Builder setNeutralButton(CharSequence text, DialogInterface.OnClickListener listener) {
            mText[2] = text;
            mListeners[2] = listener;
            return this;
        }

        public Builder setCancelable(boolean cancelable) {
            mCancelable = cancelable;
            return this;
        }

        public Builder setOnCancelListener(DialogInterface.OnCancelListener onCancelListener) {
            mOnCancel = onCancelListener;
            return this;
        }

        public Builder setOnDismissListener(DialogInterface.OnDismissListener onDismissListener) {
            mOnDismiss = onDismissListener;
            return this;
        }

        public Builder setView(View view) {
            mView = view;
            return this;
        }

        public AlertDialog create() {
            AlertDialog d = new AlertDialog(mContext);
            d.setTitle(mTitle);
            d.setMessage(mMessage);
            for (int i = 0; i < 3; i++) {
                if (mText[i] != null) d.setButton(-(i + 1), mText[i], mListeners[i]);
            }
            d.setCancelable(mCancelable);
            d.setOnCancelListener(mOnCancel);
            d.setOnDismissListener(mOnDismiss);
            if (mView != null) d.setContentView(mView);
            return d;
        }

        public AlertDialog show() {
            AlertDialog d = create();
            d.show();
            return d;
        }
    }
}
