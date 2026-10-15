package android.view.inputmethod;

import android.os.Bundle;
import android.os.Handler;
import android.view.KeyEvent;

/** The channel between an input method and the editor it edits. */
public interface InputConnection {
    int GET_TEXT_WITH_STYLES = 0x0001;
    int GET_EXTRACTED_TEXT_MONITOR = 0x0001;
    int CURSOR_UPDATE_IMMEDIATE = 1 << 0;
    int CURSOR_UPDATE_MONITOR = 1 << 1;

    CharSequence getTextBeforeCursor(int n, int flags);

    CharSequence getTextAfterCursor(int n, int flags);

    CharSequence getSelectedText(int flags);

    int getCursorCapsMode(int reqModes);

    boolean deleteSurroundingText(int beforeLength, int afterLength);

    boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength);

    boolean setComposingText(CharSequence text, int newCursorPosition);

    boolean setComposingRegion(int start, int end);

    boolean finishComposingText();

    boolean commitText(CharSequence text, int newCursorPosition);

    boolean setSelection(int start, int end);

    boolean performEditorAction(int editorAction);

    boolean performContextMenuAction(int id);

    boolean beginBatchEdit();

    boolean endBatchEdit();

    boolean sendKeyEvent(KeyEvent event);

    boolean clearMetaKeyStates(int states);

    boolean reportFullscreenMode(boolean enabled);

    boolean performPrivateCommand(String action, Bundle data);

    boolean requestCursorUpdates(int cursorUpdateMode);

    Handler getHandler();

    void closeConnection();
}
