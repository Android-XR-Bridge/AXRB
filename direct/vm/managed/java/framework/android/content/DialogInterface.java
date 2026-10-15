package android.content;

public interface DialogInterface {
    int BUTTON_POSITIVE = -1;
    int BUTTON_NEGATIVE = -2;
    int BUTTON_NEUTRAL = -3;

    void cancel();

    void dismiss();

    interface OnCancelListener {
        void onCancel(DialogInterface dialog);
    }

    interface OnDismissListener {
        void onDismiss(DialogInterface dialog);
    }

    interface OnShowListener {
        void onShow(DialogInterface dialog);
    }

    interface OnClickListener {
        void onClick(DialogInterface dialog, int which);
    }

    interface OnMultiChoiceClickListener {
        void onClick(DialogInterface dialog, int which, boolean isChecked);
    }

    interface OnKeyListener {
        boolean onKey(DialogInterface dialog, int keyCode, android.view.KeyEvent event);
    }
}
