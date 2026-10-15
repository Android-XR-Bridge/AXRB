package com.axrb.keyboardprobe;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.widget.EditText;
import android.view.inputmethod.InputMethodManager;

/** Asks for the soft keyboard the way Unreal's virtual keyboard does, on a focused text field. */
public final class KeyboardProbe extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        EditText field = new EditText(this);
        setContentView(field);
        new Handler(getMainLooper()).postDelayed(() -> {
            try {
                field.requestFocus();
                InputMethodManager input = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                boolean shown = input.showSoftInput(field, 0);
                boolean hidden = input.hideSoftInputFromWindow(field.getWindowToken(), 0);
                System.out.println("KEYBOARD_PROBE shown=" + shown + " hidden=" + hidden);
            } catch (Throwable error) {
                System.out.println("KEYBOARD_PROBE_FAILED " + error);
                error.printStackTrace(System.out);
            }
            System.exit(0);
        }, 500);
    }
}
