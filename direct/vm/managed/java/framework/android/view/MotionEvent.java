package android.view;

import android.graphics.Matrix;

/**
 * A single-pointer motion event (the desktop host has one mouse pointer, which
 * is reported as a finger on a touchscreen; the wheel as ACTION_SCROLL).
 */
public final class MotionEvent extends InputEvent {
    public static final int INVALID_POINTER_ID = -1;
    public static final int ACTION_MASK = 0xff;
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    public static final int ACTION_MOVE = 2;
    public static final int ACTION_CANCEL = 3;
    public static final int ACTION_OUTSIDE = 4;
    public static final int ACTION_POINTER_DOWN = 5;
    public static final int ACTION_POINTER_UP = 6;
    public static final int ACTION_HOVER_MOVE = 7;
    public static final int ACTION_SCROLL = 8;
    public static final int ACTION_HOVER_ENTER = 9;
    public static final int ACTION_HOVER_EXIT = 10;
    public static final int ACTION_BUTTON_PRESS = 11;
    public static final int ACTION_BUTTON_RELEASE = 12;
    public static final int ACTION_POINTER_INDEX_MASK = 0xff00;
    public static final int ACTION_POINTER_INDEX_SHIFT = 8;

    public static final int FLAG_WINDOW_IS_OBSCURED = 0x1;
    public static final int FLAG_WINDOW_IS_PARTIALLY_OBSCURED = 0x2;
    public static final int EDGE_TOP = 0x00000001;
    public static final int EDGE_BOTTOM = 0x00000002;
    public static final int EDGE_LEFT = 0x00000004;
    public static final int EDGE_RIGHT = 0x00000008;

    public static final int AXIS_X = 0;
    public static final int AXIS_Y = 1;
    public static final int AXIS_PRESSURE = 2;
    public static final int AXIS_SIZE = 3;
    public static final int AXIS_VSCROLL = 9;
    public static final int AXIS_HSCROLL = 10;

    public static final int BUTTON_PRIMARY = 1 << 0;
    public static final int BUTTON_SECONDARY = 1 << 1;
    public static final int BUTTON_TERTIARY = 1 << 2;

    public static final int TOOL_TYPE_UNKNOWN = 0;
    public static final int TOOL_TYPE_FINGER = 1;
    public static final int TOOL_TYPE_STYLUS = 2;
    public static final int TOOL_TYPE_MOUSE = 3;
    public static final int TOOL_TYPE_ERASER = 4;

    public static final class PointerCoords {
        public float x;
        public float y;
        public float pressure;
        public float size;
        public float touchMajor;
        public float touchMinor;
        public float orientation;

        public PointerCoords() {}

        public PointerCoords(PointerCoords other) {
            copyFrom(other);
        }

        public void copyFrom(PointerCoords other) {
            x = other.x;
            y = other.y;
            pressure = other.pressure;
            size = other.size;
            touchMajor = other.touchMajor;
            touchMinor = other.touchMinor;
            orientation = other.orientation;
        }

        public void clear() {
            x = y = pressure = size = touchMajor = touchMinor = orientation = 0;
        }
    }

    public static final class PointerProperties {
        public int id;
        public int toolType;

        public PointerProperties() {
            clear();
        }

        public PointerProperties(PointerProperties other) {
            id = other.id;
            toolType = other.toolType;
        }

        public void clear() {
            id = INVALID_POINTER_ID;
            toolType = TOOL_TYPE_UNKNOWN;
        }
    }

    private long downTime;
    private long eventTime;
    private int action;
    private float rawX;
    private float rawY;
    private float x;
    private float y;
    private int metaState;
    private int buttonState;
    private int flags;
    private int edgeFlags;
    private float vscroll;
    private float hscroll;
    private int source = InputDevice.SOURCE_TOUCHSCREEN;

    private MotionEvent() {}

    public static MotionEvent obtain(long downTime, long eventTime, int action, float x, float y, int metaState) {
        MotionEvent ev = new MotionEvent();
        ev.downTime = downTime;
        ev.eventTime = eventTime;
        ev.action = action;
        ev.x = ev.rawX = x;
        ev.y = ev.rawY = y;
        ev.metaState = metaState;
        return ev;
    }

    public static MotionEvent obtain(long downTime, long eventTime, int action, float x, float y, float pressure,
            float size, int metaState, float xPrecision, float yPrecision, int deviceId, int edgeFlags) {
        MotionEvent ev = obtain(downTime, eventTime, action, x, y, metaState);
        ev.edgeFlags = edgeFlags;
        return ev;
    }

    public static MotionEvent obtain(long downTime, long eventTime, int action, int pointerCount,
            PointerProperties[] pointerProperties, PointerCoords[] pointerCoords, int metaState, int buttonState,
            float xPrecision, float yPrecision, int deviceId, int edgeFlags, int source, int flags) {
        if (pointerCount != 1) throw new IllegalArgumentException("only one pointer is supported");
        MotionEvent ev = obtain(downTime, eventTime, action, pointerCoords[0].x, pointerCoords[0].y, metaState);
        ev.buttonState = buttonState;
        ev.edgeFlags = edgeFlags;
        ev.source = source;
        ev.flags = flags;
        return ev;
    }

    public static MotionEvent obtain(MotionEvent other) {
        if (other == null) throw new IllegalArgumentException("other motion event must not be null");
        MotionEvent ev = new MotionEvent();
        ev.downTime = other.downTime;
        ev.eventTime = other.eventTime;
        ev.action = other.action;
        ev.rawX = other.rawX;
        ev.rawY = other.rawY;
        ev.x = other.x;
        ev.y = other.y;
        ev.metaState = other.metaState;
        ev.buttonState = other.buttonState;
        ev.flags = other.flags;
        ev.edgeFlags = other.edgeFlags;
        ev.vscroll = other.vscroll;
        ev.hscroll = other.hscroll;
        ev.source = other.source;
        return ev;
    }

    public static MotionEvent obtainNoHistory(MotionEvent other) {
        return obtain(other);
    }

    /** A wheel event at (x, y): vertical/horizontal scroll in lines. */
    static MotionEvent obtainScroll(long time, float x, float y, float vscroll, float hscroll) {
        MotionEvent ev = obtain(time, time, ACTION_SCROLL, x, y, 0);
        ev.source = InputDevice.SOURCE_MOUSE;
        ev.vscroll = vscroll;
        ev.hscroll = hscroll;
        return ev;
    }

    public MotionEvent copy() {
        return obtain(this);
    }

    @Override
    public void recycle() {}

    public int getAction() {
        return action;
    }

    public int getActionMasked() {
        return action & ACTION_MASK;
    }

    public int getActionIndex() {
        return (action & ACTION_POINTER_INDEX_MASK) >> ACTION_POINTER_INDEX_SHIFT;
    }

    public void setAction(int action) {
        this.action = action;
    }

    public int getFlags() {
        return flags;
    }

    public long getDownTime() {
        return downTime;
    }

    public void setDownTime(long downTime) {
        this.downTime = downTime;
    }

    @Override
    public long getEventTime() {
        return eventTime;
    }

    public float getX() {
        return x;
    }

    public float getY() {
        return y;
    }

    public float getX(int pointerIndex) {
        checkPointer(pointerIndex);
        return x;
    }

    public float getY(int pointerIndex) {
        checkPointer(pointerIndex);
        return y;
    }

    public float getRawX() {
        return rawX;
    }

    public float getRawY() {
        return rawY;
    }

    public float getRawX(int pointerIndex) {
        checkPointer(pointerIndex);
        return rawX;
    }

    public float getRawY(int pointerIndex) {
        checkPointer(pointerIndex);
        return rawY;
    }

    public float getAxisValue(int axis) {
        switch (axis) {
            case AXIS_X: return x;
            case AXIS_Y: return y;
            case AXIS_PRESSURE: return 1f;
            case AXIS_VSCROLL: return vscroll;
            case AXIS_HSCROLL: return hscroll;
            default: return 0f;
        }
    }

    public float getAxisValue(int axis, int pointerIndex) {
        checkPointer(pointerIndex);
        return getAxisValue(axis);
    }

    public int getPointerCount() {
        return 1;
    }

    public int getPointerId(int pointerIndex) {
        checkPointer(pointerIndex);
        return 0;
    }

    public int findPointerIndex(int pointerId) {
        return pointerId == 0 ? 0 : -1;
    }

    public int getToolType(int pointerIndex) {
        checkPointer(pointerIndex);
        return source == InputDevice.SOURCE_MOUSE ? TOOL_TYPE_MOUSE : TOOL_TYPE_FINGER;
    }

    public void getPointerCoords(int pointerIndex, PointerCoords outPointerCoords) {
        checkPointer(pointerIndex);
        outPointerCoords.clear();
        outPointerCoords.x = x;
        outPointerCoords.y = y;
        outPointerCoords.pressure = 1f;
    }

    public void getPointerProperties(int pointerIndex, PointerProperties outPointerProperties) {
        checkPointer(pointerIndex);
        outPointerProperties.id = 0;
        outPointerProperties.toolType = getToolType(0);
    }

    public float getPressure() {
        return 1.0f;
    }

    public float getPressure(int pointerIndex) {
        checkPointer(pointerIndex);
        return 1.0f;
    }

    public float getSize() {
        return 0f;
    }

    public float getSize(int pointerIndex) {
        checkPointer(pointerIndex);
        return 0f;
    }

    public float getTouchMajor() {
        return 0f;
    }

    public float getTouchMinor() {
        return 0f;
    }

    public float getOrientation() {
        return 0f;
    }

    public int getHistorySize() {
        return 0;
    }

    public float getHistoricalX(int pos) {
        throw new IllegalArgumentException("historyPos out of range");
    }

    public float getHistoricalY(int pos) {
        throw new IllegalArgumentException("historyPos out of range");
    }

    public float getHistoricalX(int pointerIndex, int pos) {
        throw new IllegalArgumentException("historyPos out of range");
    }

    public float getHistoricalY(int pointerIndex, int pos) {
        throw new IllegalArgumentException("historyPos out of range");
    }

    public long getHistoricalEventTime(int pos) {
        throw new IllegalArgumentException("historyPos out of range");
    }

    public int getMetaState() {
        return metaState;
    }

    public int getButtonState() {
        return buttonState;
    }

    public boolean isButtonPressed(int button) {
        return button != 0 && (buttonState & button) == button;
    }

    public int getActionButton() {
        return 0;
    }

    @Override
    public int getDeviceId() {
        return 0;
    }

    @Override
    public int getSource() {
        return source;
    }

    @Override
    public void setSource(int source) {
        this.source = source;
    }

    public int getEdgeFlags() {
        return edgeFlags;
    }

    public void setEdgeFlags(int flags) {
        edgeFlags = flags;
    }

    public float getXPrecision() {
        return 1f;
    }

    public float getYPrecision() {
        return 1f;
    }

    public void offsetLocation(float deltaX, float deltaY) {
        x += deltaX;
        y += deltaY;
    }

    public void setLocation(float x, float y) {
        this.x = x;
        this.y = y;
    }

    public void transform(Matrix matrix) {
        float[] pts = {x, y};
        matrix.mapPoints(pts);
        x = pts[0];
        y = pts[1];
    }

    public void addBatch(long eventTime, float x, float y, float pressure, float size, int metaState) {
        this.eventTime = eventTime;
        this.x = x;
        this.y = y;
        this.metaState = metaState;
    }

    private static void checkPointer(int pointerIndex) {
        if (pointerIndex != 0) throw new IllegalArgumentException("pointerIndex out of range");
    }

    public static String axisToString(int axis) {
        switch (axis) {
            case AXIS_X: return "AXIS_X";
            case AXIS_Y: return "AXIS_Y";
            case AXIS_VSCROLL: return "AXIS_VSCROLL";
            case AXIS_HSCROLL: return "AXIS_HSCROLL";
            default: return Integer.toString(axis);
        }
    }

    public static String actionToString(int action) {
        switch (action & ACTION_MASK) {
            case ACTION_DOWN: return "ACTION_DOWN";
            case ACTION_UP: return "ACTION_UP";
            case ACTION_MOVE: return "ACTION_MOVE";
            case ACTION_CANCEL: return "ACTION_CANCEL";
            case ACTION_OUTSIDE: return "ACTION_OUTSIDE";
            case ACTION_SCROLL: return "ACTION_SCROLL";
            case ACTION_HOVER_MOVE: return "ACTION_HOVER_MOVE";
            case ACTION_HOVER_ENTER: return "ACTION_HOVER_ENTER";
            case ACTION_HOVER_EXIT: return "ACTION_HOVER_EXIT";
            default: return Integer.toString(action);
        }
    }

    @Override
    public String toString() {
        return "MotionEvent { action=" + actionToString(action) + ", x[0]=" + x + ", y[0]=" + y + ", eventTime="
                + eventTime + ", downTime=" + downTime + " }";
    }
}
