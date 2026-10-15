package android.view;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.os.Handler;
import android.os.IBinder;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.util.FloatProperty;
import android.util.Log;
import android.util.Property;
import android.util.SparseArray;
import android.view.animation.Animation;
import android.view.animation.Transformation;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * The base UI element: attributes from XML and styles, background and
 * foreground drawables with drawable state, measure/layout/draw, touch, keys,
 * focus, scrolling and transformations, following Android's View contracts.
 */
public class View implements Drawable.Callback, KeyEvent.Callback {
    /** Focus requested by a <requestFocus/> tag in inflated XML. */
    void restoreDefaultFocusAfterInflate() {
        View focused = findFocus();
        if (focused == null) requestFocus();
    }


    private static final String VIEW_LOG_TAG = "View";

    public static final int NO_ID = -1;
    public static final int VISIBLE = 0x00000000;
    public static final int INVISIBLE = 0x00000004;
    public static final int GONE = 0x00000008;
    static final int VISIBILITY_MASK = 0x0000000C;
    private static final int[] VISIBILITY_FLAGS = {VISIBLE, INVISIBLE, GONE};

    public static final int FOCUSABLE_AUTO = 0x00000010;
    public static final int FOCUSABLE = 0x00000001;
    public static final int NOT_FOCUSABLE = 0x00000000;
    public static final int FOCUSABLES_ALL = 0x00000000;
    public static final int FOCUSABLES_TOUCH_MODE = 0x00000001;
    public static final int FOCUS_BACKWARD = 0x00000001;
    public static final int FOCUS_FORWARD = 0x00000002;
    public static final int FOCUS_LEFT = 0x00000011;
    public static final int FOCUS_UP = 0x00000021;
    public static final int FOCUS_RIGHT = 0x00000042;
    public static final int FOCUS_DOWN = 0x00000082;

    public static final int MEASURED_SIZE_MASK = 0x00ffffff;
    public static final int MEASURED_STATE_MASK = 0xff000000;
    public static final int MEASURED_HEIGHT_STATE_SHIFT = 16;
    public static final int MEASURED_STATE_TOO_SMALL = 0x01000000;

    public static final int LAYOUT_DIRECTION_LTR = 0;
    public static final int LAYOUT_DIRECTION_RTL = 1;
    public static final int LAYOUT_DIRECTION_INHERIT = 2;
    public static final int LAYOUT_DIRECTION_LOCALE = 3;

    public static final int TEXT_DIRECTION_INHERIT = 0;
    public static final int TEXT_DIRECTION_FIRST_STRONG = 1;
    public static final int TEXT_DIRECTION_ANY_RTL = 2;
    public static final int TEXT_DIRECTION_LTR = 3;
    public static final int TEXT_DIRECTION_RTL = 4;
    public static final int TEXT_DIRECTION_LOCALE = 5;
    public static final int TEXT_DIRECTION_FIRST_STRONG_LTR = 6;
    public static final int TEXT_DIRECTION_FIRST_STRONG_RTL = 7;

    public static final int TEXT_ALIGNMENT_INHERIT = 0;
    public static final int TEXT_ALIGNMENT_GRAVITY = 1;
    public static final int TEXT_ALIGNMENT_TEXT_START = 2;
    public static final int TEXT_ALIGNMENT_TEXT_END = 3;
    public static final int TEXT_ALIGNMENT_CENTER = 4;
    public static final int TEXT_ALIGNMENT_VIEW_START = 5;
    public static final int TEXT_ALIGNMENT_VIEW_END = 6;

    public static final int SCROLLBARS_INSIDE_OVERLAY = 0;
    public static final int SCROLLBARS_INSIDE_INSET = 0x01000000;
    public static final int SCROLLBARS_OUTSIDE_OVERLAY = 0x02000000;
    public static final int SCROLLBARS_OUTSIDE_INSET = 0x03000000;
    public static final int SCROLLBAR_POSITION_DEFAULT = 0;
    public static final int SCROLLBAR_POSITION_LEFT = 1;
    public static final int SCROLLBAR_POSITION_RIGHT = 2;
    public static final int SCROLL_AXIS_NONE = 0;
    public static final int SCROLL_AXIS_HORIZONTAL = 1 << 0;
    public static final int SCROLL_AXIS_VERTICAL = 1 << 1;
    public static final int SCROLL_INDICATOR_TOP = 0x1;
    public static final int SCROLL_INDICATOR_BOTTOM = 0x2;

    public static final int OVER_SCROLL_ALWAYS = 0;
    public static final int OVER_SCROLL_IF_CONTENT_SCROLLS = 1;
    public static final int OVER_SCROLL_NEVER = 2;

    public static final int LAYER_TYPE_NONE = 0;
    public static final int LAYER_TYPE_SOFTWARE = 1;
    public static final int LAYER_TYPE_HARDWARE = 2;

    public static final int DRAWING_CACHE_QUALITY_LOW = 0x00080000;
    public static final int DRAWING_CACHE_QUALITY_HIGH = 0x00100000;
    public static final int DRAWING_CACHE_QUALITY_AUTO = 0x00000000;

    public static final int IMPORTANT_FOR_ACCESSIBILITY_AUTO = 0x00000000;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_YES = 0x00000001;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_NO = 0x00000002;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS = 0x00000004;
    public static final int ACCESSIBILITY_LIVE_REGION_NONE = 0;
    public static final int ACCESSIBILITY_LIVE_REGION_POLITE = 1;
    public static final int ACCESSIBILITY_LIVE_REGION_ASSERTIVE = 2;
    public static final int IMPORTANT_FOR_AUTOFILL_AUTO = 0;
    public static final int IMPORTANT_FOR_AUTOFILL_YES = 1;
    public static final int IMPORTANT_FOR_AUTOFILL_NO = 2;
    public static final int AUTOFILL_TYPE_NONE = 0;
    public static final int AUTOFILL_TYPE_TEXT = 1;

    public static final int SYSTEM_UI_FLAG_VISIBLE = 0;
    public static final int SYSTEM_UI_FLAG_LOW_PROFILE = 0x00000001;
    public static final int SYSTEM_UI_FLAG_HIDE_NAVIGATION = 0x00000002;
    public static final int SYSTEM_UI_FLAG_FULLSCREEN = 0x00000004;
    public static final int SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR = 0x00000010;
    public static final int SYSTEM_UI_FLAG_LAYOUT_STABLE = 0x00000100;
    public static final int SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION = 0x00000200;
    public static final int SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN = 0x00000400;
    public static final int SYSTEM_UI_FLAG_IMMERSIVE = 0x00000800;
    public static final int SYSTEM_UI_FLAG_IMMERSIVE_STICKY = 0x00001000;
    public static final int SYSTEM_UI_FLAG_LIGHT_STATUS_BAR = 0x00002000;
    public static final int STATUS_BAR_HIDDEN = SYSTEM_UI_FLAG_LOW_PROFILE;
    public static final int STATUS_BAR_VISIBLE = SYSTEM_UI_FLAG_VISIBLE;

    public static final int HAPTIC_FEEDBACK_ENABLED = 0x10000000;
    public static final int SOUND_EFFECTS_ENABLED = 0x08000000;
    public static final int KEEP_SCREEN_ON = 0x04000000;

    public static final int DRAG_FLAG_GLOBAL = 1 << 8;

    public static final int[] EMPTY_STATE_SET = {};
    public static final int[] ENABLED_STATE_SET = {android.R.attr.state_enabled};
    public static final int[] FOCUSED_STATE_SET = {android.R.attr.state_focused};
    public static final int[] SELECTED_STATE_SET = {android.R.attr.state_selected};
    public static final int[] PRESSED_STATE_SET = {android.R.attr.state_pressed};
    public static final int[] WINDOW_FOCUSED_STATE_SET = {android.R.attr.state_window_focused};
    public static final int[] ENABLED_FOCUSED_STATE_SET = {android.R.attr.state_enabled, android.R.attr.state_focused};
    public static final int[] ENABLED_SELECTED_STATE_SET = {android.R.attr.state_enabled, android.R.attr.state_selected};
    public static final int[] ENABLED_WINDOW_FOCUSED_STATE_SET = {android.R.attr.state_enabled,
            android.R.attr.state_window_focused};
    public static final int[] FOCUSED_SELECTED_STATE_SET = {android.R.attr.state_focused, android.R.attr.state_selected};
    public static final int[] PRESSED_ENABLED_STATE_SET = {android.R.attr.state_pressed, android.R.attr.state_enabled};
    public static final int[] PRESSED_FOCUSED_STATE_SET = {android.R.attr.state_pressed, android.R.attr.state_focused};
    public static final int[] PRESSED_SELECTED_STATE_SET = {android.R.attr.state_pressed, android.R.attr.state_selected};
    public static final int[] ENABLED_FOCUSED_WINDOW_FOCUSED_STATE_SET = {android.R.attr.state_enabled,
            android.R.attr.state_focused, android.R.attr.state_window_focused};
    public static final int[] PRESSED_ENABLED_FOCUSED_STATE_SET = {android.R.attr.state_pressed,
            android.R.attr.state_enabled, android.R.attr.state_focused};

    private static final AtomicInteger sNextGeneratedId = new AtomicInteger(1);

    // --- properties for animations ---------------------------------------------------

    public static final Property<View, Float> ALPHA = new FloatProperty<View>("alpha") {
        public void setValue(View object, float value) {
            object.setAlpha(value);
        }

        public Float get(View object) {
            return object.getAlpha();
        }
    };
    public static final Property<View, Float> TRANSLATION_X = new FloatProperty<View>("translationX") {
        public void setValue(View object, float value) {
            object.setTranslationX(value);
        }

        public Float get(View object) {
            return object.getTranslationX();
        }
    };
    public static final Property<View, Float> TRANSLATION_Y = new FloatProperty<View>("translationY") {
        public void setValue(View object, float value) {
            object.setTranslationY(value);
        }

        public Float get(View object) {
            return object.getTranslationY();
        }
    };
    public static final Property<View, Float> TRANSLATION_Z = new FloatProperty<View>("translationZ") {
        public void setValue(View object, float value) {
            object.setTranslationZ(value);
        }

        public Float get(View object) {
            return object.getTranslationZ();
        }
    };
    public static final Property<View, Float> X = new FloatProperty<View>("x") {
        public void setValue(View object, float value) {
            object.setX(value);
        }

        public Float get(View object) {
            return object.getX();
        }
    };
    public static final Property<View, Float> Y = new FloatProperty<View>("y") {
        public void setValue(View object, float value) {
            object.setY(value);
        }

        public Float get(View object) {
            return object.getY();
        }
    };
    public static final Property<View, Float> Z = new FloatProperty<View>("z") {
        public void setValue(View object, float value) {
            object.setZ(value);
        }

        public Float get(View object) {
            return object.getZ();
        }
    };
    public static final Property<View, Float> ROTATION = new FloatProperty<View>("rotation") {
        public void setValue(View object, float value) {
            object.setRotation(value);
        }

        public Float get(View object) {
            return object.getRotation();
        }
    };
    public static final Property<View, Float> ROTATION_X = new FloatProperty<View>("rotationX") {
        public void setValue(View object, float value) {
            object.setRotationX(value);
        }

        public Float get(View object) {
            return object.getRotationX();
        }
    };
    public static final Property<View, Float> ROTATION_Y = new FloatProperty<View>("rotationY") {
        public void setValue(View object, float value) {
            object.setRotationY(value);
        }

        public Float get(View object) {
            return object.getRotationY();
        }
    };
    public static final Property<View, Float> SCALE_X = new FloatProperty<View>("scaleX") {
        public void setValue(View object, float value) {
            object.setScaleX(value);
        }

        public Float get(View object) {
            return object.getScaleX();
        }
    };
    public static final Property<View, Float> SCALE_Y = new FloatProperty<View>("scaleY") {
        public void setValue(View object, float value) {
            object.setScaleY(value);
        }

        public Float get(View object) {
            return object.getScaleY();
        }
    };

    // --- listener interfaces ---------------------------------------------------------

    public interface OnClickListener {
        void onClick(View v);
    }

    public interface OnLongClickListener {
        boolean onLongClick(View v);
    }

    public interface OnContextClickListener {
        boolean onContextClick(View v);
    }

    public interface OnTouchListener {
        boolean onTouch(View v, MotionEvent event);
    }

    public interface OnFocusChangeListener {
        void onFocusChange(View v, boolean hasFocus);
    }

    public interface OnKeyListener {
        boolean onKey(View v, int keyCode, KeyEvent event);
    }

    public interface OnGenericMotionListener {
        boolean onGenericMotion(View v, MotionEvent event);
    }

    public interface OnHoverListener {
        boolean onHover(View v, MotionEvent event);
    }

    public interface OnAttachStateChangeListener {
        void onViewAttachedToWindow(View v);

        void onViewDetachedFromWindow(View v);
    }

    public interface OnLayoutChangeListener {
        void onLayoutChange(View v, int left, int top, int right, int bottom, int oldLeft, int oldTop, int oldRight,
                int oldBottom);
    }

    public interface OnScrollChangeListener {
        void onScrollChange(View v, int scrollX, int scrollY, int oldScrollX, int oldScrollY);
    }

    public interface OnSystemUiVisibilityChangeListener {
        void onSystemUiVisibilityChange(int visibility);
    }

    public interface OnApplyWindowInsetsListener {
        WindowInsets onApplyWindowInsets(View v, WindowInsets insets);
    }

    public interface OnCreateContextMenuListener {
        void onCreateContextMenu(ContextMenu menu, View v, ContextMenu.ContextMenuInfo menuInfo);
    }

    public interface OnDragListener {
        boolean onDrag(View v, DragEvent event);
    }

    public static class MeasureSpec {
        private static final int MODE_SHIFT = 30;
        private static final int MODE_MASK = 0x3 << MODE_SHIFT;
        public static final int UNSPECIFIED = 0 << MODE_SHIFT;
        public static final int EXACTLY = 1 << MODE_SHIFT;
        public static final int AT_MOST = 2 << MODE_SHIFT;

        public static int makeMeasureSpec(int size, int mode) {
            return (size & ~MODE_MASK) | (mode & MODE_MASK);
        }

        public static int makeSafeMeasureSpec(int size, int mode) {
            return makeMeasureSpec(size, mode);
        }

        public static int getMode(int measureSpec) {
            return measureSpec & MODE_MASK;
        }

        public static int getSize(int measureSpec) {
            return measureSpec & ~MODE_MASK;
        }

        static int adjust(int measureSpec, int delta) {
            final int mode = getMode(measureSpec);
            int size = getSize(measureSpec);
            if (mode == UNSPECIFIED) return makeMeasureSpec(size, UNSPECIFIED);
            size += delta;
            if (size < 0) size = 0;
            return makeMeasureSpec(size, mode);
        }

        public static String toString(int measureSpec) {
            int mode = getMode(measureSpec);
            int size = getSize(measureSpec);
            StringBuilder sb = new StringBuilder("MeasureSpec: ");
            if (mode == UNSPECIFIED) sb.append("UNSPECIFIED ");
            else if (mode == EXACTLY) sb.append("EXACTLY ");
            else if (mode == AT_MOST) sb.append("AT_MOST ");
            else sb.append(mode).append(' ');
            return sb.append(size).toString();
        }
    }

    public static class BaseSavedState extends AbsSavedState {
        public BaseSavedState(Parcel source) {
            super(source);
        }

        public BaseSavedState(Parcel source, ClassLoader loader) {
            super(source, loader);
        }

        public BaseSavedState(Parcelable superState) {
            super(superState);
        }

        public static final Parcelable.Creator<BaseSavedState> CREATOR = new Parcelable.ClassLoaderCreator<BaseSavedState>() {
            public BaseSavedState createFromParcel(Parcel in) {
                return new BaseSavedState(in);
            }

            public BaseSavedState createFromParcel(Parcel in, ClassLoader loader) {
                return new BaseSavedState(in, loader);
            }

            public BaseSavedState[] newArray(int size) {
                return new BaseSavedState[size];
            }
        };
    }

    /** State shared by all views of one window (AOSP View.AttachInfo). */
    static final class AttachInfo {
        final ViewRootImpl mViewRootImpl;
        final Handler mHandler;
        View mRootView;
        final IBinder mWindowToken;
        final ViewTreeObserver mTreeObserver = new ViewTreeObserver();
        final KeyEvent.DispatcherState mKeyDispatchState = new KeyEvent.DispatcherState();
        boolean mHasWindowFocus;
        int mWindowVisibility = VISIBLE;
        boolean mInTouchMode = true;
        boolean mKeepScreenOn;
        int mSystemUiVisibility;
        long mDrawingTime;
        final Rect mTmpInvalRect = new Rect();
        final int[] mTmpLocation = new int[2];

        AttachInfo(ViewRootImpl root, Handler handler, View rootView, IBinder token) {
            mViewRootImpl = root;
            mHandler = handler;
            mRootView = rootView;
            mWindowToken = token;
        }
    }

    /** A click handler named by android:onClick, looked up on the view's context. */
    private static class DeclaredOnClickListener implements OnClickListener {
        private final View mHostView;
        private final String mMethodName;
        private Method mResolvedMethod;
        private Context mResolvedContext;

        DeclaredOnClickListener(View hostView, String methodName) {
            mHostView = hostView;
            mMethodName = methodName;
        }

        @Override
        public void onClick(View v) {
            if (mResolvedMethod == null) resolveMethod(mHostView.getContext());
            try {
                mResolvedMethod.invoke(mResolvedContext, v);
            } catch (IllegalAccessException e) {
                throw new IllegalStateException("Could not execute non-public method for android:onClick", e);
            } catch (InvocationTargetException e) {
                throw new IllegalStateException("Could not execute method for android:onClick", e);
            }
        }

        private void resolveMethod(Context context) {
            while (context != null) {
                try {
                    if (!context.isRestricted()) {
                        Method method = context.getClass().getMethod(mMethodName, View.class);
                        if (method != null) {
                            mResolvedMethod = method;
                            mResolvedContext = context;
                            return;
                        }
                    }
                } catch (NoSuchMethodException e) {
                    // Try the base context.
                }
                context = context instanceof ContextWrapper ? ((ContextWrapper) context).getBaseContext() : null;
            }
            final int id = mHostView.getId();
            final String idText = id == NO_ID ? "" : " with id '" + mHostView.getContext().getResources()
                    .getResourceEntryName(id) + "'";
            throw new IllegalStateException("Could not find method " + mMethodName
                    + "(View) in a parent or ancestor Context for android:onClick attribute defined on view "
                    + mHostView.getClass() + idText);
        }
    }

    // --- state ----------------------------------------------------------------------

    protected Context mContext;
    protected ViewParent mParent;
    AttachInfo mAttachInfo;

    protected int mLeft;
    protected int mTop;
    protected int mRight;
    protected int mBottom;
    protected int mPaddingLeft;
    protected int mPaddingTop;
    protected int mPaddingRight;
    protected int mPaddingBottom;
    protected int mScrollX;
    protected int mScrollY;
    private int mUserPaddingStart = Integer.MIN_VALUE;
    private int mUserPaddingEnd = Integer.MIN_VALUE;

    private int mMeasuredWidth;
    private int mMeasuredHeight;
    private boolean mMeasuredDimensionSet;
    int mOldWidthMeasureSpec = Integer.MIN_VALUE;
    int mOldHeightMeasureSpec = Integer.MIN_VALUE;
    boolean mForceLayout = true;
    boolean mLayoutRequired;
    private boolean mIsLaidOut;
    private int mMinWidth;
    private int mMinHeight;

    protected ViewGroup.LayoutParams mLayoutParams;
    private int mId = NO_ID;
    private Object mTag;
    private SparseArray<Object> mKeyedTags;
    private int mVisibility = VISIBLE;
    private boolean mEnabled = true;
    private boolean mClickable;
    private boolean mLongClickable;
    private boolean mContextClickable;
    private boolean mPressed;
    private int mFocusable = FOCUSABLE_AUTO;
    private boolean mFocusableInTouchMode;
    boolean mFocused;
    private boolean mSelected;
    private boolean mActivated;
    private boolean mHovered;
    private boolean mDuplicateParentState;
    private boolean mSaveEnabled = true;
    private boolean mWillNotDraw;
    private boolean mFitsSystemWindows;
    private boolean mKeepScreenOn;
    private boolean mSoundEffectsEnabled = true;
    private boolean mHapticFeedbackEnabled = true;
    private boolean mHasPerformedLongPress;
    private boolean mInContextButtonPress;
    private int mTransientStateCount;
    private int mImportantForAccessibility;
    private int mAccessibilityLiveRegion;
    private int mImportantForAutofill;
    private int mLayoutDirection = LAYOUT_DIRECTION_INHERIT;
    private int mTextDirection = TEXT_DIRECTION_INHERIT;
    private int mTextAlignment = TEXT_ALIGNMENT_GRAVITY;
    private int mLayerType = LAYER_TYPE_NONE;
    private int mOverScrollMode = OVER_SCROLL_IF_CONTENT_SCROLLS;
    private boolean mNestedScrollingEnabled;
    private int mSystemUiVisibility;
    private int mNextFocusLeftId = NO_ID;
    private int mNextFocusRightId = NO_ID;
    private int mNextFocusUpId = NO_ID;
    private int mNextFocusDownId = NO_ID;
    private int mNextFocusForwardId = NO_ID;
    private boolean mFocusedByDefault;
    private CharSequence mContentDescription;
    private CharSequence mTooltipText;
    private String mTransitionName;
    private int mLabelForId = NO_ID;

    private int[] mDrawableState;
    private boolean mDrawableStateDirty = true;
    private Drawable mBackground;
    private ColorStateList mBackgroundTintList;
    private PorterDuff.Mode mBackgroundTintMode;
    private boolean mBackgroundSizeChanged = true;
    private Drawable mForeground;
    private int mForegroundGravity = Gravity.FILL;
    private ColorStateList mForegroundTintList;
    private final Rect mForegroundSelfBounds = new Rect();
    private final Rect mForegroundOverlayBounds = new Rect();

    private float mTranslationX;
    private float mTranslationY;
    private float mTranslationZ;
    private float mElevation;
    private float mRotation;
    private float mRotationX;
    private float mRotationY;
    private float mScaleX = 1f;
    private float mScaleY = 1f;
    private float mPivotX;
    private float mPivotY;
    private boolean mPivotExplicitlySet;
    private float mAlpha = 1f;
    private float mTransitionAlpha = 1f;
    private float mCameraDistance;
    private Matrix mMatrix;
    private Matrix mInverseMatrix;
    private boolean mMatrixDirty = true;
    private ViewOutlineProvider mOutlineProvider = ViewOutlineProvider.BACKGROUND;
    private boolean mClipToOutline;
    private Rect mClipBounds;

    private boolean mVerticalScrollBarEnabled;
    private boolean mHorizontalScrollBarEnabled;
    private boolean mScrollbarFadingEnabled = true;
    private int mScrollBarStyle = SCROLLBARS_INSIDE_OVERLAY;
    private int mVerticalScrollbarPosition;
    private int mScrollBarDefaultDelayBeforeFade = ViewConfiguration.getScrollDefaultDelay();
    private int mScrollBarFadeDuration = ViewConfiguration.getScrollBarFadeDuration();
    private long mScrollBarFadeStartTime;
    private Paint mScrollBarPaint;
    private boolean mVerticalFadingEdgeEnabled;
    private boolean mHorizontalFadingEdgeEnabled;
    private int mFadingEdgeLength;

    private OnClickListener mOnClickListener;
    private OnLongClickListener mOnLongClickListener;
    private OnContextClickListener mOnContextClickListener;
    private OnTouchListener mOnTouchListener;
    private OnFocusChangeListener mOnFocusChangeListener;
    private OnKeyListener mOnKeyListener;
    private OnGenericMotionListener mOnGenericMotionListener;
    private OnHoverListener mOnHoverListener;
    private OnScrollChangeListener mOnScrollChangeListener;
    private OnSystemUiVisibilityChangeListener mOnSystemUiVisibilityChangeListener;
    private OnApplyWindowInsetsListener mOnApplyWindowInsetsListener;
    private OnCreateContextMenuListener mOnCreateContextMenuListener;
    private OnDragListener mOnDragListener;
    private ArrayList<OnAttachStateChangeListener> mAttachListeners;
    private ArrayList<OnLayoutChangeListener> mLayoutChangeListeners;
    private TouchDelegate mTouchDelegate;
    private AccessibilityDelegate mAccessibilityDelegate;

    private ArrayList<Runnable> mRunQueue;
    private ViewTreeObserver mFloatingTreeObserver;
    private Runnable mPerformClick;
    private Runnable mUnsetPressedState;
    private Runnable mPendingCheckForLongPress;
    private Runnable mPendingCheckForTap;
    private int mWindowAttachCount;

    private Animation mCurrentAnimation;
    private final Transformation mAnimationTransformation = new Transformation();
    private ViewPropertyAnimator mAnimator;

    private Bitmap mDrawingCache;
    private boolean mDrawingCacheEnabled;
    private int mDrawingCacheBackgroundColor;

    // --- construction -------------------------------------------------------------------

    public View(Context context) {
        mContext = context;
        if (context != null) {
            mFadingEdgeLength = ViewConfiguration.get(context).getScaledFadingEdgeLength();
        }
    }

    public View(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public View(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public View(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        this(context);
        final TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.View,
                defStyleAttr, defStyleRes);
        Drawable background = null;
        int leftPadding = -1, topPadding = -1, rightPadding = -1, bottomPadding = -1;
        int startPadding = Integer.MIN_VALUE, endPadding = Integer.MIN_VALUE;
        int padding = -1, paddingHorizontal = -1, paddingVertical = -1;
        boolean leftPaddingDefined = false, rightPaddingDefined = false;
        int x = 0, y = 0;
        float tx = 0, ty = 0, tz = 0, elevation = 0, rotation = 0, sx = 1f, sy = 1f;
        boolean transformSet = false;
        int scrollbars = 0;
        final int n = a.getIndexCount();
        for (int i = 0; i < n; i++) {
            int attr = a.getIndex(i);
            switch (attr) {
                case android.R.styleable.View_background:
                    background = a.getDrawable(attr);
                    break;
                case android.R.styleable.View_padding:
                    padding = a.getDimensionPixelSize(attr, -1);
                    leftPaddingDefined = true;
                    rightPaddingDefined = true;
                    break;
                case android.R.styleable.View_paddingHorizontal:
                    paddingHorizontal = a.getDimensionPixelSize(attr, -1);
                    leftPaddingDefined = true;
                    rightPaddingDefined = true;
                    break;
                case android.R.styleable.View_paddingVertical:
                    paddingVertical = a.getDimensionPixelSize(attr, -1);
                    break;
                case android.R.styleable.View_paddingLeft:
                    leftPadding = a.getDimensionPixelSize(attr, -1);
                    leftPaddingDefined = true;
                    break;
                case android.R.styleable.View_paddingTop:
                    topPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case android.R.styleable.View_paddingRight:
                    rightPadding = a.getDimensionPixelSize(attr, -1);
                    rightPaddingDefined = true;
                    break;
                case android.R.styleable.View_paddingBottom:
                    bottomPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case android.R.styleable.View_paddingStart:
                    startPadding = a.getDimensionPixelSize(attr, Integer.MIN_VALUE);
                    break;
                case android.R.styleable.View_paddingEnd:
                    endPadding = a.getDimensionPixelSize(attr, Integer.MIN_VALUE);
                    break;
                case android.R.styleable.View_scrollX:
                    x = a.getDimensionPixelOffset(attr, 0);
                    break;
                case android.R.styleable.View_scrollY:
                    y = a.getDimensionPixelOffset(attr, 0);
                    break;
                case android.R.styleable.View_alpha:
                    setAlpha(a.getFloat(attr, 1f));
                    break;
                case android.R.styleable.View_transformPivotX:
                    setPivotX(a.getDimension(attr, 0));
                    break;
                case android.R.styleable.View_transformPivotY:
                    setPivotY(a.getDimension(attr, 0));
                    break;
                case android.R.styleable.View_translationX:
                    tx = a.getDimension(attr, 0);
                    transformSet = true;
                    break;
                case android.R.styleable.View_translationY:
                    ty = a.getDimension(attr, 0);
                    transformSet = true;
                    break;
                case android.R.styleable.View_translationZ:
                    tz = a.getDimension(attr, 0);
                    transformSet = true;
                    break;
                case android.R.styleable.View_elevation:
                    elevation = a.getDimension(attr, 0);
                    transformSet = true;
                    break;
                case android.R.styleable.View_rotation:
                    rotation = a.getFloat(attr, 0);
                    transformSet = true;
                    break;
                case android.R.styleable.View_scaleX:
                    sx = a.getFloat(attr, 1f);
                    transformSet = true;
                    break;
                case android.R.styleable.View_scaleY:
                    sy = a.getFloat(attr, 1f);
                    transformSet = true;
                    break;
                case android.R.styleable.View_id:
                    mId = a.getResourceId(attr, NO_ID);
                    break;
                case android.R.styleable.View_tag:
                    mTag = a.getText(attr);
                    break;
                case android.R.styleable.View_fitsSystemWindows:
                    mFitsSystemWindows = a.getBoolean(attr, false);
                    break;
                case android.R.styleable.View_focusable: {
                    int type = a.getType(attr);
                    if (type == android.util.TypedValue.TYPE_INT_BOOLEAN) {
                        mFocusable = a.getBoolean(attr, false) ? FOCUSABLE : NOT_FOCUSABLE;
                    } else {
                        mFocusable = a.getInt(attr, FOCUSABLE_AUTO);
                    }
                    break;
                }
                case android.R.styleable.View_focusableInTouchMode:
                    mFocusableInTouchMode = a.getBoolean(attr, false);
                    if (mFocusableInTouchMode) mFocusable = FOCUSABLE;
                    break;
                case android.R.styleable.View_clickable:
                    mClickable = a.getBoolean(attr, false);
                    break;
                case android.R.styleable.View_longClickable:
                    mLongClickable = a.getBoolean(attr, false);
                    break;
                case android.R.styleable.View_saveEnabled:
                    mSaveEnabled = a.getBoolean(attr, true);
                    break;
                case android.R.styleable.View_duplicateParentState:
                    mDuplicateParentState = a.getBoolean(attr, false);
                    break;
                case android.R.styleable.View_visibility: {
                    final int visibility = a.getInt(attr, 0);
                    if (visibility >= 0 && visibility < VISIBILITY_FLAGS.length) {
                        mVisibility = VISIBILITY_FLAGS[visibility];
                    }
                    break;
                }
                case android.R.styleable.View_layoutDirection:
                    mLayoutDirection = a.getInt(attr, LAYOUT_DIRECTION_INHERIT);
                    break;
                case android.R.styleable.View_contentDescription:
                    mContentDescription = a.getText(attr);
                    break;
                case android.R.styleable.View_tooltipText:
                    mTooltipText = a.getText(attr);
                    break;
                case android.R.styleable.View_soundEffectsEnabled:
                    mSoundEffectsEnabled = a.getBoolean(attr, true);
                    break;
                case android.R.styleable.View_hapticFeedbackEnabled:
                    mHapticFeedbackEnabled = a.getBoolean(attr, true);
                    break;
                case android.R.styleable.View_scrollbars:
                    scrollbars = a.getInt(attr, 0);
                    break;
                case android.R.styleable.View_enabled:
                    mEnabled = a.getBoolean(attr, true);
                    break;
                case android.R.styleable.View_minWidth:
                    mMinWidth = a.getDimensionPixelSize(attr, 0);
                    break;
                case android.R.styleable.View_minHeight:
                    mMinHeight = a.getDimensionPixelSize(attr, 0);
                    break;
                case android.R.styleable.View_onClick: {
                    final String handlerName = a.getString(attr);
                    if (handlerName != null) setOnClickListener(new DeclaredOnClickListener(this, handlerName));
                    break;
                }
                case android.R.styleable.View_overScrollMode:
                    mOverScrollMode = a.getInt(attr, OVER_SCROLL_IF_CONTENT_SCROLLS);
                    break;
                case android.R.styleable.View_keepScreenOn:
                    mKeepScreenOn = a.getBoolean(attr, false);
                    break;
                case android.R.styleable.View_nextFocusLeft:
                    mNextFocusLeftId = a.getResourceId(attr, NO_ID);
                    break;
                case android.R.styleable.View_nextFocusRight:
                    mNextFocusRightId = a.getResourceId(attr, NO_ID);
                    break;
                case android.R.styleable.View_nextFocusUp:
                    mNextFocusUpId = a.getResourceId(attr, NO_ID);
                    break;
                case android.R.styleable.View_nextFocusDown:
                    mNextFocusDownId = a.getResourceId(attr, NO_ID);
                    break;
                case android.R.styleable.View_nextFocusForward:
                    mNextFocusForwardId = a.getResourceId(attr, NO_ID);
                    break;
                case android.R.styleable.View_focusedByDefault:
                    mFocusedByDefault = a.getBoolean(attr, false);
                    break;
                case android.R.styleable.View_textDirection:
                    mTextDirection = a.getInt(attr, TEXT_DIRECTION_INHERIT);
                    break;
                case android.R.styleable.View_textAlignment:
                    mTextAlignment = a.getInt(attr, TEXT_ALIGNMENT_GRAVITY);
                    break;
                case android.R.styleable.View_importantForAccessibility:
                    mImportantForAccessibility = a.getInt(attr, IMPORTANT_FOR_ACCESSIBILITY_AUTO);
                    break;
                case android.R.styleable.View_foreground:
                    setForeground(a.getDrawable(attr));
                    break;
                case android.R.styleable.View_foregroundGravity:
                    mForegroundGravity = a.getInt(attr, Gravity.FILL);
                    break;
                case android.R.styleable.View_backgroundTint:
                    mBackgroundTintList = a.getColorStateList(attr);
                    break;
                case android.R.styleable.View_backgroundTintMode:
                    mBackgroundTintMode = Drawable.parseTintMode(a.getInt(attr, -1), null);
                    break;
                case android.R.styleable.View_nestedScrollingEnabled:
                    mNestedScrollingEnabled = a.getBoolean(attr, false);
                    break;
                case android.R.styleable.View_clipToOutline:
                    mClipToOutline = a.getBoolean(attr, false);
                    break;
                default:
                    break;
            }
        }
        a.recycle();

        if (background != null) setBackground(background);
        // Explicit padding wins over the background's padding, side by side.
        if (padding >= 0) {
            leftPadding = topPadding = rightPadding = bottomPadding = padding;
        } else {
            if (paddingHorizontal >= 0) {
                leftPadding = rightPadding = paddingHorizontal;
            }
            if (paddingVertical >= 0) {
                topPadding = bottomPadding = paddingVertical;
            }
        }
        mUserPaddingStart = startPadding;
        mUserPaddingEnd = endPadding;
        if (startPadding != Integer.MIN_VALUE) {
            leftPadding = startPadding;
            leftPaddingDefined = true;
        }
        if (endPadding != Integer.MIN_VALUE) {
            rightPadding = endPadding;
            rightPaddingDefined = true;
        }
        internalSetPadding(leftPaddingDefined && leftPadding >= 0 ? leftPadding : mPaddingLeft,
                topPadding >= 0 ? topPadding : mPaddingTop,
                rightPaddingDefined && rightPadding >= 0 ? rightPadding : mPaddingRight,
                bottomPadding >= 0 ? bottomPadding : mPaddingBottom);
        if (x != 0 || y != 0) scrollTo(x, y);
        if (transformSet) {
            setTranslationX(tx);
            setTranslationY(ty);
            setTranslationZ(tz);
            setElevation(elevation);
            setRotation(rotation);
            setScaleX(sx);
            setScaleY(sy);
        }
        mVerticalScrollBarEnabled = (scrollbars & 0x200) != 0;
        mHorizontalScrollBarEnabled = (scrollbars & 0x100) != 0;
        if (mBackgroundTintList != null || mBackgroundTintMode != null) applyBackgroundTint();
        if (mFocusable == FOCUSABLE_AUTO && (mClickable || mLongClickable)) {
            // FOCUSABLE_AUTO: clickable views are keyboard-focusable.
        }
    }

    // --- context and resources -------------------------------------------------------------

    public final Context getContext() {
        return mContext;
    }

    public Resources getResources() {
        return mContext == null ? null : mContext.getResources();
    }

    final float density() {
        Resources r = getResources();
        return r == null ? 1f : r.getDisplayMetrics().density;
    }

    public static View inflate(Context context, int resource, ViewGroup root) {
        LayoutInflater factory = LayoutInflater.from(context);
        return factory.inflate(resource, root);
    }

    // --- identity ---------------------------------------------------------------------------

    public static int generateViewId() {
        for (;;) {
            final int result = sNextGeneratedId.get();
            int newValue = result + 1;
            if (newValue > 0x00FFFFFF) newValue = 1;
            if (sNextGeneratedId.compareAndSet(result, newValue)) return result;
        }
    }

    public void setId(int id) {
        mId = id;
    }

    public int getId() {
        return mId;
    }

    public Object getTag() {
        return mTag;
    }

    public void setTag(Object tag) {
        mTag = tag;
    }

    public Object getTag(int key) {
        return mKeyedTags != null ? mKeyedTags.get(key) : null;
    }

    public void setTag(int key, Object tag) {
        if ((key >>> 24) < 2) {
            throw new IllegalArgumentException("The key must be an application-specific resource id.");
        }
        if (mKeyedTags == null) mKeyedTags = new SparseArray<Object>(2);
        mKeyedTags.put(key, tag);
    }

    public CharSequence getContentDescription() {
        return mContentDescription;
    }

    public void setContentDescription(CharSequence contentDescription) {
        mContentDescription = contentDescription;
    }

    public CharSequence getTooltipText() {
        return mTooltipText;
    }

    public void setTooltipText(CharSequence tooltipText) {
        mTooltipText = tooltipText;
    }

    public String getTransitionName() {
        return mTransitionName;
    }

    public final void setTransitionName(String transitionName) {
        mTransitionName = transitionName;
    }

    public int getLabelFor() {
        return mLabelForId;
    }

    public void setLabelFor(int id) {
        mLabelForId = id;
    }

    public final <T extends View> T findViewById(int id) {
        if (id == NO_ID) return null;
        return findViewTraversal(id);
    }

    public final <T extends View> T requireViewById(int id) {
        T view = findViewById(id);
        if (view == null) throw new IllegalArgumentException("ID does not reference a View inside this View");
        return view;
    }

    @SuppressWarnings("unchecked")
    protected <T extends View> T findViewTraversal(int id) {
        return id == mId ? (T) this : null;
    }

    public final <T extends View> T findViewWithTag(Object tag) {
        if (tag == null) return null;
        return findViewWithTagTraversal(tag);
    }

    @SuppressWarnings("unchecked")
    protected <T extends View> T findViewWithTagTraversal(Object tag) {
        return tag.equals(mTag) ? (T) this : null;
    }

    public void findViewsWithText(ArrayList<View> outViews, CharSequence searched, int flags) {
        if (searched == null || mContentDescription == null) return;
        if (mContentDescription.toString().toLowerCase().contains(searched.toString().toLowerCase())) {
            outViews.add(this);
        }
    }

    public static final int FIND_VIEWS_WITH_TEXT = 0x00000001;
    public static final int FIND_VIEWS_WITH_CONTENT_DESCRIPTION = 0x00000002;

    // --- hierarchy and window ---------------------------------------------------------------

    public final ViewParent getParent() {
        return mParent;
    }

    void assignParent(ViewParent parent) {
        if (mParent == null || parent == null) {
            mParent = parent;
        } else {
            throw new RuntimeException("view " + this + " being added, but it already has a parent");
        }
    }

    public View getRootView() {
        if (mAttachInfo != null && mAttachInfo.mRootView != null) return mAttachInfo.mRootView;
        View v = this;
        while (v.mParent instanceof View) v = (View) v.mParent;
        return v;
    }

    public boolean isAttachedToWindow() {
        return mAttachInfo != null;
    }

    public boolean isLaidOut() {
        return mIsLaidOut;
    }

    public ViewRootImpl getViewRootImpl() {
        return mAttachInfo != null ? mAttachInfo.mViewRootImpl : null;
    }

    public Handler getHandler() {
        return mAttachInfo != null ? mAttachInfo.mHandler : null;
    }

    public IBinder getWindowToken() {
        return mAttachInfo != null ? mAttachInfo.mWindowToken : null;
    }

    public IBinder getApplicationWindowToken() {
        return getWindowToken();
    }

    public Display getDisplay() {
        return mAttachInfo != null ? Display.getDefault() : null;
    }

    public int getWindowVisibility() {
        return mAttachInfo != null ? mAttachInfo.mWindowVisibility : GONE;
    }

    public boolean hasWindowFocus() {
        return mAttachInfo != null && mAttachInfo.mHasWindowFocus;
    }

    public void getWindowVisibleDisplayFrame(Rect outRect) {
        if (mAttachInfo != null) {
            mAttachInfo.mViewRootImpl.getWindowVisibleDisplayFrame(outRect);
        } else {
            android.util.DisplayMetrics m = getResources().getDisplayMetrics();
            outRect.set(0, 0, m.widthPixels, m.heightPixels);
        }
    }

    public ViewTreeObserver getViewTreeObserver() {
        if (mAttachInfo != null) return mAttachInfo.mTreeObserver;
        if (mFloatingTreeObserver == null) mFloatingTreeObserver = new ViewTreeObserver();
        return mFloatingTreeObserver;
    }

    public boolean post(Runnable action) {
        if (mAttachInfo != null) return mAttachInfo.mHandler.post(action);
        if (mRunQueue == null) mRunQueue = new ArrayList<Runnable>();
        mRunQueue.add(action);
        return true;
    }

    public boolean postDelayed(Runnable action, long delayMillis) {
        if (mAttachInfo != null) return mAttachInfo.mHandler.postDelayed(action, delayMillis);
        // Runs once the view is attached, as Android's run queue does.
        return post(action);
    }

    public void postOnAnimation(Runnable action) {
        Choreographer.getInstance().postCallback(Choreographer.CALLBACK_ANIMATION, action, null);
    }

    public void postOnAnimationDelayed(Runnable action, long delayMillis) {
        Choreographer.getInstance().postCallbackDelayed(Choreographer.CALLBACK_ANIMATION, action, null, delayMillis);
    }

    public boolean removeCallbacks(Runnable action) {
        if (action != null) {
            if (mAttachInfo != null) mAttachInfo.mHandler.removeCallbacks(action);
            if (mRunQueue != null) mRunQueue.remove(action);
            Choreographer.getInstance().removeCallbacks(Choreographer.CALLBACK_ANIMATION, action, null);
        }
        return true;
    }

    public void postInvalidate() {
        postInvalidateDelayed(0);
    }

    public void postInvalidate(int left, int top, int right, int bottom) {
        postInvalidateDelayed(0);
    }

    public void postInvalidateDelayed(long delayMilliseconds) {
        Handler h = mAttachInfo != null ? mAttachInfo.mHandler : null;
        if (h == null) return;
        h.postDelayed(new Runnable() {
            public void run() {
                invalidate();
            }
        }, delayMilliseconds);
    }

    public void postInvalidateOnAnimation() {
        postOnAnimation(new Runnable() {
            public void run() {
                invalidate();
            }
        });
    }

    void dispatchAttachedToWindow(AttachInfo info, int visibility) {
        mAttachInfo = info;
        mWindowAttachCount++;
        if (mFloatingTreeObserver != null) {
            info.mTreeObserver.merge(mFloatingTreeObserver);
            mFloatingTreeObserver = null;
        }
        if (mRunQueue != null) {
            for (Runnable r : mRunQueue) info.mHandler.post(r);
            mRunQueue = null;
        }
        if (mKeepScreenOn) info.mKeepScreenOn = true;
        mDrawableStateDirty = true;
        onAttachedToWindow();
        if (mAttachListeners != null) {
            for (OnAttachStateChangeListener l : new ArrayList<OnAttachStateChangeListener>(mAttachListeners)) {
                l.onViewAttachedToWindow(this);
            }
        }
        int vis = info.mWindowVisibility;
        if (vis != GONE) onWindowVisibilityChanged(vis);
        refreshDrawableState();
        if (mBackground != null) mBackground.setVisible(vis == VISIBLE && mVisibility == VISIBLE, false);
    }

    void dispatchDetachedFromWindow() {
        if (mAttachInfo != null && mAttachInfo.mWindowVisibility != GONE) onWindowVisibilityChanged(GONE);
        removeUnsetPressCallback();
        removeLongPressCallback();
        removePerformClickCallback();
        removeTapCallback();
        if (mBackground != null) mBackground.setVisible(false, false);
        onDetachedFromWindow();
        if (mAttachListeners != null) {
            for (OnAttachStateChangeListener l : new ArrayList<OnAttachStateChangeListener>(mAttachListeners)) {
                l.onViewDetachedFromWindow(this);
            }
        }
        if (mCurrentAnimation != null) {
            mCurrentAnimation.cancel();
            mCurrentAnimation = null;
        }
        mAttachInfo = null;
    }

    protected void onAttachedToWindow() {}

    protected void onDetachedFromWindow() {}

    protected int getWindowAttachCount() {
        return mWindowAttachCount;
    }

    public void onWindowFocusChanged(boolean hasWindowFocus) {
        if (!hasWindowFocus) {
            if (mPressed) setPressed(false);
            removeLongPressCallback();
            removeTapCallback();
        }
        refreshDrawableState();
    }

    public void dispatchWindowFocusChanged(boolean hasFocus) {
        onWindowFocusChanged(hasFocus);
    }

    protected void onWindowVisibilityChanged(int visibility) {}

    public void dispatchWindowVisibilityChanged(int visibility) {
        onWindowVisibilityChanged(visibility);
    }

    protected void onVisibilityChanged(View changedView, int visibility) {}

    protected void dispatchVisibilityChanged(View changedView, int visibility) {
        onVisibilityChanged(changedView, visibility);
    }

    public void onVisibilityAggregated(boolean isVisible) {
        if (mBackground != null && isVisible != mBackground.isVisible()) mBackground.setVisible(isVisible, false);
        if (mForeground != null && isVisible != mForeground.isVisible()) mForeground.setVisible(isVisible, false);
    }

    public void addOnAttachStateChangeListener(OnAttachStateChangeListener listener) {
        if (mAttachListeners == null) mAttachListeners = new ArrayList<OnAttachStateChangeListener>();
        mAttachListeners.add(listener);
    }

    public void removeOnAttachStateChangeListener(OnAttachStateChangeListener listener) {
        if (mAttachListeners != null) mAttachListeners.remove(listener);
    }

    public void addOnLayoutChangeListener(OnLayoutChangeListener listener) {
        if (mLayoutChangeListeners == null) mLayoutChangeListeners = new ArrayList<OnLayoutChangeListener>();
        if (!mLayoutChangeListeners.contains(listener)) mLayoutChangeListeners.add(listener);
    }

    public void removeOnLayoutChangeListener(OnLayoutChangeListener listener) {
        if (mLayoutChangeListeners != null) mLayoutChangeListeners.remove(listener);
    }

    protected void onFinishInflate() {}

    // --- visibility and enabled state ------------------------------------------------------

    public int getVisibility() {
        return mVisibility;
    }

    public void setVisibility(int visibility) {
        visibility &= VISIBILITY_MASK;
        if (visibility == mVisibility) return;
        final int old = mVisibility;
        mVisibility = visibility;
        if (visibility != VISIBLE && hasFocus()) clearFocus();
        if (visibility == GONE || old == GONE) {
            requestLayout();
        }
        invalidateParentForChange();
        if (mParent instanceof ViewGroup) ((ViewGroup) mParent).onChildVisibilityChanged(this, old, visibility);
        dispatchVisibilityChanged(this, visibility);
        boolean visible = visibility == VISIBLE && getWindowVisibility() == VISIBLE;
        if (mAttachInfo != null) onVisibilityAggregated(visible && isShownParents());
    }

    private boolean isShownParents() {
        ViewParent p = mParent;
        while (p instanceof View) {
            if (((View) p).mVisibility != VISIBLE) return false;
            p = ((View) p).mParent;
        }
        return true;
    }

    public boolean isShown() {
        View v = this;
        for (;;) {
            if (v.mVisibility != VISIBLE) return false;
            ViewParent p = v.mParent;
            if (p == null) return false;
            if (!(p instanceof View)) return true;
            v = (View) p;
        }
    }

    public boolean isEnabled() {
        return mEnabled;
    }

    public void setEnabled(boolean enabled) {
        if (mEnabled == enabled) return;
        mEnabled = enabled;
        if (!enabled) {
            if (mPressed) setPressed(false);
            if (isFocused()) clearFocus();
        }
        refreshDrawableState();
        invalidate();
    }

    public boolean isClickable() {
        return mClickable;
    }

    public void setClickable(boolean clickable) {
        mClickable = clickable;
    }

    public boolean isLongClickable() {
        return mLongClickable;
    }

    public void setLongClickable(boolean longClickable) {
        mLongClickable = longClickable;
    }

    public boolean isContextClickable() {
        return mContextClickable;
    }

    public void setContextClickable(boolean contextClickable) {
        mContextClickable = contextClickable;
    }

    public boolean isPressed() {
        return mPressed;
    }

    public void setPressed(boolean pressed) {
        if (mPressed == pressed) return;
        mPressed = pressed;
        refreshDrawableState();
        dispatchSetPressed(pressed);
    }

    private void setPressed(boolean pressed, float x, float y) {
        if (pressed) drawableHotspotChanged(x, y);
        setPressed(pressed);
    }

    protected void dispatchSetPressed(boolean pressed) {}

    public boolean isSelected() {
        return mSelected;
    }

    public void setSelected(boolean selected) {
        if (mSelected != selected) {
            mSelected = selected;
            invalidate();
            refreshDrawableState();
            dispatchSetSelected(selected);
        }
    }

    protected void dispatchSetSelected(boolean selected) {}

    public boolean isActivated() {
        return mActivated;
    }

    public void setActivated(boolean activated) {
        if (mActivated != activated) {
            mActivated = activated;
            invalidate();
            refreshDrawableState();
            dispatchSetActivated(activated);
        }
    }

    protected void dispatchSetActivated(boolean activated) {}

    public boolean isHovered() {
        return mHovered;
    }

    public void setHovered(boolean hovered) {
        if (mHovered != hovered) {
            mHovered = hovered;
            refreshDrawableState();
            onHoverChanged(hovered);
        }
    }

    public void onHoverChanged(boolean hovered) {}

    public void setDuplicateParentStateEnabled(boolean enabled) {
        mDuplicateParentState = enabled;
    }

    public boolean isDuplicateParentStateEnabled() {
        return mDuplicateParentState;
    }

    public boolean hasTransientState() {
        return mTransientStateCount > 0;
    }

    public void setHasTransientState(boolean hasTransientState) {
        mTransientStateCount = hasTransientState ? mTransientStateCount + 1 : Math.max(0, mTransientStateCount - 1);
    }

    public boolean isOpaque() {
        return mBackground != null && mBackground.getOpacity() == android.graphics.PixelFormat.OPAQUE
                && getFinalAlpha() >= 1.0f;
    }

    // --- focus ---------------------------------------------------------------------------------

    public void setFocusable(boolean focusable) {
        setFocusable(focusable ? FOCUSABLE : NOT_FOCUSABLE);
    }

    public void setFocusable(int focusable) {
        if ((focusable & (FOCUSABLE_AUTO | FOCUSABLE)) == 0) setFocusableInTouchMode(false);
        mFocusable = focusable;
        if (!isFocusable() && isFocused()) clearFocus();
    }

    public void setFocusableInTouchMode(boolean focusableInTouchMode) {
        mFocusableInTouchMode = focusableInTouchMode;
        if (focusableInTouchMode) {
            mFocusable = FOCUSABLE;
        } else if (isFocused() && isInTouchMode()) {
            clearFocus();
        }
    }

    public final boolean isFocusable() {
        return mFocusable == FOCUSABLE || (mFocusable == FOCUSABLE_AUTO && (mClickable || mLongClickable));
    }

    public int getFocusable() {
        return mFocusable;
    }

    public final boolean isFocusableInTouchMode() {
        return mFocusableInTouchMode;
    }

    public boolean isFocused() {
        return mFocused;
    }

    public boolean hasFocus() {
        return mFocused;
    }

    public boolean hasFocusable() {
        return mVisibility == VISIBLE && isFocusable() && mEnabled;
    }

    public boolean hasExplicitFocusable() {
        return mVisibility == VISIBLE && mFocusable == FOCUSABLE && mEnabled;
    }

    public View findFocus() {
        return mFocused ? this : null;
    }

    public boolean isFocusedByDefault() {
        return mFocusedByDefault;
    }

    public void setFocusedByDefault(boolean isFocusedByDefault) {
        mFocusedByDefault = isFocusedByDefault;
    }

    public boolean isInTouchMode() {
        return mAttachInfo == null || mAttachInfo.mInTouchMode;
    }

    public final boolean requestFocus() {
        return requestFocus(FOCUS_DOWN);
    }

    public final boolean requestFocus(int direction) {
        return requestFocus(direction, null);
    }

    public boolean requestFocus(int direction, Rect previouslyFocusedRect) {
        return requestFocusNoSearch(direction, previouslyFocusedRect);
    }

    boolean canTakeFocus() {
        return mVisibility == VISIBLE && isFocusable() && mEnabled;
    }

    boolean requestFocusNoSearch(int direction, Rect previouslyFocusedRect) {
        if (!canTakeFocus()) return false;
        if (isInTouchMode() && !mFocusableInTouchMode) return false;
        if (hasAncestorThatBlocksDescendantFocus()) return false;
        handleFocusGainInternal(direction, previouslyFocusedRect);
        return true;
    }

    public final boolean requestFocusFromTouch() {
        if (mAttachInfo != null) mAttachInfo.mViewRootImpl.ensureTouchMode(false);
        return requestFocus(FOCUS_DOWN);
    }

    private boolean hasAncestorThatBlocksDescendantFocus() {
        ViewParent ancestor = mParent;
        while (ancestor instanceof ViewGroup) {
            final ViewGroup vgAncestor = (ViewGroup) ancestor;
            if (vgAncestor.getDescendantFocusability() == ViewGroup.FOCUS_BLOCK_DESCENDANTS) return true;
            ancestor = vgAncestor.getParent();
        }
        return false;
    }

    void handleFocusGainInternal(int direction, Rect previouslyFocusedRect) {
        if (mFocused) return;
        View oldFocus = mAttachInfo != null ? getRootView().findFocus() : null;
        mFocused = true;
        if (mParent != null) mParent.requestChildFocus(this, this);
        if (mAttachInfo != null) mAttachInfo.mTreeObserver.dispatchOnGlobalFocusChange(oldFocus, this);
        onFocusChanged(true, direction, previouslyFocusedRect);
        refreshDrawableState();
    }

    public void clearFocus() {
        if (!mFocused) return;
        clearFocusInternal(true, true);
    }

    void clearFocusInternal(boolean propagate, boolean refocus) {
        if (!mFocused) return;
        mFocused = false;
        if (propagate && mParent != null) mParent.clearChildFocus(this);
        onFocusChanged(false, 0, null);
        refreshDrawableState();
        if (propagate && mAttachInfo != null) {
            mAttachInfo.mTreeObserver.dispatchOnGlobalFocusChange(this, null);
            if (refocus) mAttachInfo.mViewRootImpl.focusFirstIfNotInTouchMode();
        }
    }

    void unFocus(View focused) {
        clearFocusInternal(false, false);
    }

    protected void onFocusChanged(boolean gainFocus, int direction, Rect previouslyFocusedRect) {
        if (!gainFocus) {
            if (mPressed) setPressed(false);
        }
        invalidate();
        if (mOnFocusChangeListener != null) mOnFocusChangeListener.onFocusChange(this, gainFocus);
    }

    public void setOnFocusChangeListener(OnFocusChangeListener l) {
        mOnFocusChangeListener = l;
    }

    public OnFocusChangeListener getOnFocusChangeListener() {
        return mOnFocusChangeListener;
    }

    public View focusSearch(int direction) {
        if (mParent != null) return mParent.focusSearch(this, direction);
        return null;
    }

    public void addFocusables(ArrayList<View> views, int direction) {
        addFocusables(views, direction, isInTouchMode() ? FOCUSABLES_TOUCH_MODE : FOCUSABLES_ALL);
    }

    public void addFocusables(ArrayList<View> views, int direction, int focusableMode) {
        if (views == null) return;
        if (!canTakeFocus()) return;
        if ((focusableMode & FOCUSABLES_TOUCH_MODE) == FOCUSABLES_TOUCH_MODE && !mFocusableInTouchMode) return;
        views.add(this);
    }

    public ArrayList<View> getFocusables(int direction) {
        ArrayList<View> result = new ArrayList<View>(24);
        addFocusables(result, direction);
        return result;
    }

    public void addTouchables(ArrayList<View> views) {
        if ((mClickable || mLongClickable) && mEnabled) views.add(this);
    }

    public ArrayList<View> getTouchables() {
        ArrayList<View> result = new ArrayList<View>();
        addTouchables(result);
        return result;
    }

    public void getFocusedRect(Rect r) {
        getDrawingRect(r);
    }

    public int getNextFocusLeftId() {
        return mNextFocusLeftId;
    }

    public void setNextFocusLeftId(int id) {
        mNextFocusLeftId = id;
    }

    public int getNextFocusRightId() {
        return mNextFocusRightId;
    }

    public void setNextFocusRightId(int id) {
        mNextFocusRightId = id;
    }

    public int getNextFocusUpId() {
        return mNextFocusUpId;
    }

    public void setNextFocusUpId(int id) {
        mNextFocusUpId = id;
    }

    public int getNextFocusDownId() {
        return mNextFocusDownId;
    }

    public void setNextFocusDownId(int id) {
        mNextFocusDownId = id;
    }

    public int getNextFocusForwardId() {
        return mNextFocusForwardId;
    }

    public void setNextFocusForwardId(int id) {
        mNextFocusForwardId = id;
    }

    int nextFocusIdFor(int direction) {
        switch (direction) {
            case FOCUS_LEFT: return mNextFocusLeftId;
            case FOCUS_RIGHT: return mNextFocusRightId;
            case FOCUS_UP: return mNextFocusUpId;
            case FOCUS_DOWN: return mNextFocusDownId;
            case FOCUS_FORWARD: return mNextFocusForwardId;
            default: return NO_ID;
        }
    }

    public boolean onCheckIsTextEditor() {
        return false;
    }

    public android.view.inputmethod.InputConnection onCreateInputConnection(
            android.view.inputmethod.EditorInfo outAttrs) {
        return null;
    }

    public boolean checkInputConnectionProxy(View view) {
        return false;
    }

    // --- keys ----------------------------------------------------------------------------------

    public void setOnKeyListener(OnKeyListener l) {
        mOnKeyListener = l;
    }

    public KeyEvent.DispatcherState getKeyDispatcherState() {
        return mAttachInfo != null ? mAttachInfo.mKeyDispatchState : null;
    }

    public boolean dispatchKeyEventPreIme(KeyEvent event) {
        return onKeyPreIme(event.getKeyCode(), event);
    }

    public boolean dispatchKeyEvent(KeyEvent event) {
        if (mOnKeyListener != null && mEnabled && mOnKeyListener.onKey(this, event.getKeyCode(), event)) return true;
        return event.dispatch(this, mAttachInfo != null ? mAttachInfo.mKeyDispatchState : null, this);
    }

    public boolean dispatchKeyShortcutEvent(KeyEvent event) {
        return onKeyShortcut(event.getKeyCode(), event);
    }

    public boolean onKeyPreIme(int keyCode, KeyEvent event) {
        return false;
    }

    static boolean isConfirmKey(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_DPAD_CENTER:
            case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_SPACE:
            case KeyEvent.KEYCODE_NUMPAD_ENTER:
                return true;
            default:
                return false;
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (isConfirmKey(keyCode)) {
            if (!mEnabled) return true;
            if (event.getRepeatCount() == 0 && (mClickable || mLongClickable)) {
                setPressed(true, getWidth() / 2f, getHeight() / 2f);
                checkForLongClick(ViewConfiguration.getLongPressTimeout());
                return true;
            }
        }
        return false;
    }

    @Override
    public boolean onKeyLongPress(int keyCode, KeyEvent event) {
        return false;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (isConfirmKey(keyCode)) {
            if (!mEnabled) return true;
            if ((mClickable || mLongClickable) && mPressed) {
                setPressed(false);
                if (!mHasPerformedLongPress) {
                    removeLongPressCallback();
                    if (!event.isCanceled()) return performClickInternal();
                }
            }
        }
        return false;
    }

    @Override
    public boolean onKeyMultiple(int keyCode, int repeatCount, KeyEvent event) {
        return false;
    }

    public boolean onKeyShortcut(int keyCode, KeyEvent event) {
        return false;
    }

    // --- clicks and touch ----------------------------------------------------------------------

    public void setOnClickListener(OnClickListener l) {
        if (!mClickable) setClickable(true);
        mOnClickListener = l;
    }

    public boolean hasOnClickListeners() {
        return mOnClickListener != null;
    }

    public void setOnLongClickListener(OnLongClickListener l) {
        if (!mLongClickable) setLongClickable(true);
        mOnLongClickListener = l;
    }

    public boolean hasOnLongClickListeners() {
        return mOnLongClickListener != null;
    }

    public void setOnContextClickListener(OnContextClickListener l) {
        if (!mContextClickable) setContextClickable(true);
        mOnContextClickListener = l;
    }

    public void setOnCreateContextMenuListener(OnCreateContextMenuListener l) {
        if (!mLongClickable) setLongClickable(true);
        mOnCreateContextMenuListener = l;
    }

    public void setOnTouchListener(OnTouchListener l) {
        mOnTouchListener = l;
    }

    public void setOnGenericMotionListener(OnGenericMotionListener l) {
        mOnGenericMotionListener = l;
    }

    public void setOnHoverListener(OnHoverListener l) {
        mOnHoverListener = l;
    }

    public void setOnDragListener(OnDragListener l) {
        mOnDragListener = l;
    }

    public boolean performClick() {
        final boolean result;
        if (mOnClickListener != null) {
            playSoundEffect(SoundEffectConstants.CLICK);
            mOnClickListener.onClick(this);
            result = true;
        } else {
            result = false;
        }
        return result;
    }

    private boolean performClickInternal() {
        return performClick();
    }

    public boolean callOnClick() {
        if (mOnClickListener != null) {
            mOnClickListener.onClick(this);
            return true;
        }
        return false;
    }

    public boolean performLongClick() {
        return performLongClickInternal(Float.NaN, Float.NaN);
    }

    public boolean performLongClick(float x, float y) {
        return performLongClickInternal(x, y);
    }

    private boolean performLongClickInternal(float x, float y) {
        boolean handled = false;
        if (mOnLongClickListener != null) handled = mOnLongClickListener.onLongClick(this);
        if (!handled) handled = showContextMenu();
        if (handled) performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
        return handled;
    }

    public boolean performContextClick(float x, float y) {
        return performContextClick();
    }

    public boolean performContextClick() {
        boolean handled = false;
        if (mOnContextClickListener != null) handled = mOnContextClickListener.onContextClick(this);
        return handled;
    }

    public boolean showContextMenu() {
        return mParent != null && mParent.showContextMenuForChild(this);
    }

    public boolean showContextMenu(float x, float y) {
        return showContextMenu();
    }

    public void createContextMenu(ContextMenu menu) {
        ContextMenu.ContextMenuInfo menuInfo = getContextMenuInfo();
        onCreateContextMenu(menu);
        if (mOnCreateContextMenuListener != null) {
            mOnCreateContextMenuListener.onCreateContextMenu(menu, this, menuInfo);
        }
        if (mParent != null) mParent.createContextMenu(menu);
    }

    protected ContextMenu.ContextMenuInfo getContextMenuInfo() {
        return null;
    }

    protected void onCreateContextMenu(ContextMenu menu) {}

    public void cancelLongPress() {
        removeLongPressCallback();
        removeTapCallback();
    }

    public void setTouchDelegate(TouchDelegate delegate) {
        mTouchDelegate = delegate;
    }

    public TouchDelegate getTouchDelegate() {
        return mTouchDelegate;
    }

    public final boolean dispatchPointerEvent(MotionEvent event) {
        if (event.isFromSource(InputDevice.SOURCE_CLASS_POINTER) && event.getActionMasked() != MotionEvent.ACTION_SCROLL
                && event.getActionMasked() != MotionEvent.ACTION_HOVER_MOVE) {
            return dispatchTouchEvent(event);
        }
        return dispatchGenericMotionEvent(event);
    }

    public boolean dispatchTouchEvent(MotionEvent event) {
        boolean result = false;
        if (onFilterTouchEventForSecurity(event)) {
            if (mOnTouchListener != null && mEnabled && mOnTouchListener.onTouch(this, event)) result = true;
            if (!result && onTouchEvent(event)) result = true;
        }
        final int actionMasked = event.getActionMasked();
        if (actionMasked == MotionEvent.ACTION_UP || actionMasked == MotionEvent.ACTION_CANCEL) {
            stopNestedScroll();
        }
        return result;
    }

    public boolean onFilterTouchEventForSecurity(MotionEvent event) {
        return true;
    }

    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (mOnGenericMotionListener != null && mEnabled && mOnGenericMotionListener.onGenericMotion(this, event)) {
            return true;
        }
        return onGenericMotionEvent(event);
    }

    public boolean onGenericMotionEvent(MotionEvent event) {
        return false;
    }

    public boolean dispatchHoverEvent(MotionEvent event) {
        if (mOnHoverListener != null && mEnabled && mOnHoverListener.onHover(this, event)) return true;
        return onHoverEvent(event);
    }

    public boolean onHoverEvent(MotionEvent event) {
        return false;
    }

    public boolean onTouchEvent(MotionEvent event) {
        final float x = event.getX();
        final float y = event.getY();
        final int action = event.getActionMasked();
        final boolean clickable = mClickable || mLongClickable || mContextClickable;
        if (!mEnabled) {
            if (action == MotionEvent.ACTION_UP && mPressed) setPressed(false);
            return clickable;
        }
        if (mTouchDelegate != null && mTouchDelegate.onTouchEvent(event)) return true;
        if (!clickable) return false;
        switch (action) {
            case MotionEvent.ACTION_DOWN: {
                mHasPerformedLongPress = false;
                if (isInScrollingContainer()) {
                    // Delay the pressed state briefly in case this is a scroll.
                    if (mPendingCheckForTap == null) mPendingCheckForTap = new CheckForTap();
                    ((CheckForTap) mPendingCheckForTap).x = x;
                    ((CheckForTap) mPendingCheckForTap).y = y;
                    postDelayed(mPendingCheckForTap, ViewConfiguration.getTapTimeout());
                } else {
                    setPressed(true, x, y);
                    checkForLongClick(ViewConfiguration.getLongPressTimeout());
                }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                drawableHotspotChanged(x, y);
                if (!pointInView(x, y, touchSlop())) {
                    removeTapCallback();
                    removeLongPressCallback();
                    if (mPressed) setPressed(false);
                }
                break;
            case MotionEvent.ACTION_UP: {
                boolean prepressed = mPendingCheckForTap != null && isTapPending;
                if (mPressed || prepressed) {
                    boolean focusTaken = false;
                    if (isFocusable() && isFocusableInTouchMode() && !isFocused()) focusTaken = requestFocus();
                    if (prepressed) setPressed(true, x, y);
                    if (!mHasPerformedLongPress) {
                        removeLongPressCallback();
                        if (!focusTaken) {
                            if (mPerformClick == null) {
                                mPerformClick = new Runnable() {
                                    public void run() {
                                        performClickInternal();
                                    }
                                };
                            }
                            if (!post(mPerformClick)) performClickInternal();
                        }
                    }
                    if (mUnsetPressedState == null) {
                        mUnsetPressedState = new Runnable() {
                            public void run() {
                                setPressed(false);
                            }
                        };
                    }
                    if (prepressed) {
                        postDelayed(mUnsetPressedState, ViewConfiguration.getPressedStateDuration());
                    } else if (!post(mUnsetPressedState)) {
                        mUnsetPressedState.run();
                    }
                    removeTapCallback();
                }
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                if (mPressed) setPressed(false);
                removeTapCallback();
                removeLongPressCallback();
                mHasPerformedLongPress = false;
                break;
        }
        return true;
    }

    private boolean isTapPending;

    private final class CheckForTap implements Runnable {
        float x;
        float y;

        @Override
        public void run() {
            isTapPending = false;
            setPressed(true, x, y);
            checkForLongClick(ViewConfiguration.getLongPressTimeout() - ViewConfiguration.getTapTimeout());
        }
    }

    public boolean isInScrollingContainer() {
        ViewParent p = getParent();
        while (p instanceof ViewGroup) {
            if (((ViewGroup) p).shouldDelayChildPressedState()) return true;
            p = p.getParent();
        }
        return false;
    }

    private int touchSlop() {
        return mContext != null ? ViewConfiguration.get(mContext).getScaledTouchSlop() : 8;
    }

    public boolean pointInView(float localX, float localY, float slop) {
        return localX >= -slop && localY >= -slop && localX < ((mRight - mLeft) + slop)
                && localY < ((mBottom - mTop) + slop);
    }

    private void checkForLongClick(long delay) {
        if (!mLongClickable) return;
        mHasPerformedLongPress = false;
        if (mPendingCheckForLongPress == null) {
            mPendingCheckForLongPress = new Runnable() {
                public void run() {
                    if (mPressed && mParent != null && performLongClick()) mHasPerformedLongPress = true;
                }
            };
        }
        postDelayed(mPendingCheckForLongPress, delay);
    }

    private void removeLongPressCallback() {
        if (mPendingCheckForLongPress != null) removeCallbacks(mPendingCheckForLongPress);
    }

    private void removePerformClickCallback() {
        if (mPerformClick != null) removeCallbacks(mPerformClick);
    }

    private void removeUnsetPressCallback() {
        if (mPressed && mUnsetPressedState != null) {
            setPressed(false);
            removeCallbacks(mUnsetPressedState);
        }
    }

    private void removeTapCallback() {
        if (mPendingCheckForTap != null) {
            isTapPending = false;
            removeCallbacks(mPendingCheckForTap);
        }
    }

    boolean hasPendingTap() {
        return isTapPending;
    }

    public final void cancelPendingInputEvents() {
        removePerformClickCallback();
        cancelLongPress();
    }

    void cancelPendingInputEventsRecursive() {
        cancelPendingInputEvents();
        if (this instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) this;
            for (int i = 0; i < group.getChildCount(); i++) group.getChildAt(i).cancelPendingInputEventsRecursive();
        }
    }

    void refreshDrawableStateRecursive() {
        refreshDrawableState();
        if (this instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) this;
            for (int i = 0; i < group.getChildCount(); i++) group.getChildAt(i).refreshDrawableStateRecursive();
        }
    }

    // --- drawable state ------------------------------------------------------------------------

    public final int[] getDrawableState() {
        if (mDrawableState != null && !mDrawableStateDirty) return mDrawableState;
        mDrawableState = onCreateDrawableState(0);
        mDrawableStateDirty = false;
        return mDrawableState;
    }

    protected int[] onCreateDrawableState(int extraSpace) {
        if (mDuplicateParentState && mParent instanceof View) {
            return ((View) mParent).onCreateDrawableState(extraSpace);
        }
        int[] states = new int[8 + extraSpace];
        int n = 0;
        if (mPressed) states[n++] = android.R.attr.state_pressed;
        if (mVisibility == VISIBLE && mEnabled) states[n++] = android.R.attr.state_enabled;
        if (isFocused()) states[n++] = android.R.attr.state_focused;
        if (mSelected) states[n++] = android.R.attr.state_selected;
        if (hasWindowFocus()) states[n++] = android.R.attr.state_window_focused;
        if (mActivated) states[n++] = android.R.attr.state_activated;
        if (mHovered) states[n++] = android.R.attr.state_hovered;
        int[] out = new int[n + extraSpace];
        System.arraycopy(states, 0, out, 0, n);
        return out;
    }

    protected static int[] mergeDrawableStates(int[] baseState, int[] additionalState) {
        final int N = baseState.length;
        int i = N - 1;
        while (i >= 0 && baseState[i] == 0) i--;
        System.arraycopy(additionalState, 0, baseState, i + 1, additionalState.length);
        return baseState;
    }

    public void refreshDrawableState() {
        mDrawableStateDirty = true;
        drawableStateChanged();
        ViewParent parent = mParent;
        if (parent != null) parent.childDrawableStateChanged(this);
    }

    protected void drawableStateChanged() {
        final int[] state = getDrawableState();
        boolean changed = false;
        if (mBackground != null && mBackground.isStateful()) changed |= mBackground.setState(state);
        if (mForeground != null && mForeground.isStateful()) changed |= mForeground.setState(state);
        if (changed) invalidate();
    }

    public void drawableHotspotChanged(float x, float y) {
        if (mBackground != null) mBackground.setHotspot(x, y);
        if (mForeground != null) mForeground.setHotspot(x, y);
    }

    public void dispatchDrawableHotspotChanged(float x, float y) {}

    protected boolean verifyDrawable(Drawable who) {
        return who == mBackground || who == mForeground;
    }

    public void jumpDrawablesToCurrentState() {
        if (mBackground != null) mBackground.jumpToCurrentState();
        if (mForeground != null) mForeground.jumpToCurrentState();
    }

    @Override
    public void invalidateDrawable(Drawable drawable) {
        if (verifyDrawable(drawable)) invalidate();
    }

    @Override
    public void scheduleDrawable(Drawable who, Runnable what, long when) {
        if (verifyDrawable(who) && what != null) {
            final long delay = when - SystemClock.uptimeMillis();
            if (mAttachInfo != null) {
                mAttachInfo.mHandler.postDelayed(what, Math.max(0, delay));
            } else {
                postDelayed(what, delay);
            }
        }
    }

    @Override
    public void unscheduleDrawable(Drawable who, Runnable what) {
        if (verifyDrawable(who) && what != null) removeCallbacks(what);
    }

    public void unscheduleDrawable(Drawable who) {}

    // --- background and foreground -------------------------------------------------------------

    public void setBackgroundColor(int color) {
        if (mBackground instanceof ColorDrawable) {
            ((ColorDrawable) mBackground.mutate()).setColor(color);
            invalidate();
        } else {
            setBackground(new ColorDrawable(color));
        }
    }

    public void setBackgroundResource(int resid) {
        if (resid != 0 && resid == mBackgroundResource) return;
        Drawable d = null;
        if (resid != 0) d = mContext.getDrawable(resid);
        setBackground(d);
        mBackgroundResource = resid;
    }

    private int mBackgroundResource;

    public void setBackground(Drawable background) {
        setBackgroundDrawable(background);
    }

    @Deprecated
    public void setBackgroundDrawable(Drawable background) {
        mBackgroundResource = 0;
        if (background == mBackground) return;
        boolean requestLayout = false;
        if (mBackground != null) {
            if (isAttachedToWindow()) mBackground.setVisible(false, false);
            mBackground.setCallback(null);
            unscheduleDrawable(mBackground);
        }
        if (background != null) {
            Rect padding = new Rect();
            if (background.getPadding(padding)) {
                // A background with padding sets the view's padding (AOSP behavior).
                internalSetPadding(padding.left, padding.top, padding.right, padding.bottom);
            }
            if (mBackground == null || mBackground.getMinimumHeight() != background.getMinimumHeight()
                    || mBackground.getMinimumWidth() != background.getMinimumWidth()) {
                requestLayout = true;
            }
            mBackground = background;
            if (background.isStateful()) background.setState(getDrawableState());
            background.setLayoutDirection(getLayoutDirection());
            if (isAttachedToWindow()) background.setVisible(getWindowVisibility() == VISIBLE && isShown(), false);
            background.setCallback(this);
            applyBackgroundTint();
        } else {
            mBackground = null;
            requestLayout = true;
        }
        mBackgroundSizeChanged = true;
        if (requestLayout) requestLayout();
        invalidate();
    }

    public Drawable getBackground() {
        return mBackground;
    }

    public void setBackgroundTintList(ColorStateList tint) {
        mBackgroundTintList = tint;
        applyBackgroundTint();
    }

    public ColorStateList getBackgroundTintList() {
        return mBackgroundTintList;
    }

    public void setBackgroundTintMode(PorterDuff.Mode tintMode) {
        mBackgroundTintMode = tintMode;
        applyBackgroundTint();
    }

    public PorterDuff.Mode getBackgroundTintMode() {
        return mBackgroundTintMode;
    }

    private void applyBackgroundTint() {
        if (mBackground != null && (mBackgroundTintList != null || mBackgroundTintMode != null)) {
            mBackground = mBackground.mutate();
            if (mBackgroundTintList != null) mBackground.setTintList(mBackgroundTintList);
            if (mBackgroundTintMode != null) mBackground.setTintMode(mBackgroundTintMode);
            if (mBackground.isStateful()) mBackground.setState(getDrawableState());
        }
    }

    public Drawable getForeground() {
        return mForeground;
    }

    public void setForeground(Drawable foreground) {
        if (mForeground == foreground) return;
        if (mForeground != null) {
            if (isAttachedToWindow()) mForeground.setVisible(false, false);
            mForeground.setCallback(null);
            unscheduleDrawable(mForeground);
        }
        mForeground = foreground;
        if (foreground != null) {
            foreground.setLayoutDirection(getLayoutDirection());
            if (foreground.isStateful()) foreground.setState(getDrawableState());
            if (mForegroundTintList != null) foreground.setTintList(mForegroundTintList);
            if (isAttachedToWindow()) foreground.setVisible(getWindowVisibility() == VISIBLE && isShown(), false);
            foreground.setCallback(this);
        }
        requestLayout();
        invalidate();
    }

    public int getForegroundGravity() {
        return mForegroundGravity;
    }

    public void setForegroundGravity(int gravity) {
        if (mForegroundGravity != gravity) {
            if ((gravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK) == 0) gravity |= Gravity.START;
            if ((gravity & Gravity.VERTICAL_GRAVITY_MASK) == 0) gravity |= Gravity.TOP;
            mForegroundGravity = gravity;
            requestLayout();
        }
    }

    public void setForegroundTintList(ColorStateList tint) {
        mForegroundTintList = tint;
        if (mForeground != null) {
            mForeground = mForeground.mutate();
            mForeground.setTintList(tint);
        }
    }

    public ColorStateList getForegroundTintList() {
        return mForegroundTintList;
    }

    // --- padding --------------------------------------------------------------------------------

    public void setPadding(int left, int top, int right, int bottom) {
        mUserPaddingStart = Integer.MIN_VALUE;
        mUserPaddingEnd = Integer.MIN_VALUE;
        internalSetPadding(left, top, right, bottom);
    }

    protected void internalSetPadding(int left, int top, int right, int bottom) {
        boolean changed = false;
        if (mPaddingLeft != left) {
            changed = true;
            mPaddingLeft = left;
        }
        if (mPaddingTop != top) {
            changed = true;
            mPaddingTop = top;
        }
        if (mPaddingRight != right) {
            changed = true;
            mPaddingRight = right;
        }
        if (mPaddingBottom != bottom) {
            changed = true;
            mPaddingBottom = bottom;
        }
        if (changed) {
            requestLayout();
            invalidateOutline();
        }
    }

    public void setPaddingRelative(int start, int top, int end, int bottom) {
        mUserPaddingStart = start;
        mUserPaddingEnd = end;
        internalSetPadding(start, top, end, bottom);
    }

    public int getPaddingLeft() {
        return mPaddingLeft;
    }

    public int getPaddingTop() {
        return mPaddingTop;
    }

    public int getPaddingRight() {
        return mPaddingRight;
    }

    public int getPaddingBottom() {
        return mPaddingBottom;
    }

    public int getPaddingStart() {
        return mPaddingLeft;
    }

    public int getPaddingEnd() {
        return mPaddingRight;
    }

    public boolean isPaddingRelative() {
        return mUserPaddingStart != Integer.MIN_VALUE || mUserPaddingEnd != Integer.MIN_VALUE;
    }

    public boolean isPaddingOffsetRequired() {
        return false;
    }

    protected int getLeftPaddingOffset() {
        return 0;
    }

    protected int getRightPaddingOffset() {
        return 0;
    }

    protected int getTopPaddingOffset() {
        return 0;
    }

    protected int getBottomPaddingOffset() {
        return 0;
    }

    public void resolvePadding() {}

    // --- directions --------------------------------------------------------------------------

    public int getLayoutDirection() {
        return LAYOUT_DIRECTION_LTR;
    }

    public int getRawLayoutDirection() {
        return mLayoutDirection;
    }

    public void setLayoutDirection(int layoutDirection) {
        if (mLayoutDirection != layoutDirection) {
            mLayoutDirection = layoutDirection;
            requestLayout();
            invalidate();
        }
    }

    public boolean isLayoutRtl() {
        return getLayoutDirection() == LAYOUT_DIRECTION_RTL;
    }

    public boolean canResolveLayoutDirection() {
        return true;
    }

    public void onRtlPropertiesChanged(int layoutDirection) {}

    public int getTextDirection() {
        return mTextDirection == TEXT_DIRECTION_INHERIT ? TEXT_DIRECTION_FIRST_STRONG : mTextDirection;
    }

    public void setTextDirection(int textDirection) {
        mTextDirection = textDirection;
        requestLayout();
        invalidate();
    }

    public int getTextAlignment() {
        if (mTextAlignment == TEXT_ALIGNMENT_INHERIT && mParent instanceof View) {
            return ((View) mParent).getTextAlignment();
        }
        return mTextAlignment == TEXT_ALIGNMENT_INHERIT ? TEXT_ALIGNMENT_GRAVITY : mTextAlignment;
    }

    public void setTextAlignment(int textAlignment) {
        if (textAlignment != mTextAlignment) {
            mTextAlignment = textAlignment;
            requestLayout();
            invalidate();
        }
    }

    // --- geometry ---------------------------------------------------------------------------------

    public final int getLeft() {
        return mLeft;
    }

    public final int getTop() {
        return mTop;
    }

    public final int getRight() {
        return mRight;
    }

    public final int getBottom() {
        return mBottom;
    }

    public final int getWidth() {
        return mRight - mLeft;
    }

    public final int getHeight() {
        return mBottom - mTop;
    }

    public final void setLeft(int left) {
        if (left != mLeft) {
            int oldWidth = mRight - mLeft;
            mLeft = left;
            sizeChange(mRight - mLeft, mBottom - mTop, oldWidth, mBottom - mTop);
        }
    }

    public final void setTop(int top) {
        if (top != mTop) {
            int oldHeight = mBottom - mTop;
            mTop = top;
            sizeChange(mRight - mLeft, mBottom - mTop, mRight - mLeft, oldHeight);
        }
    }

    public final void setRight(int right) {
        if (right != mRight) {
            int oldWidth = mRight - mLeft;
            mRight = right;
            sizeChange(mRight - mLeft, mBottom - mTop, oldWidth, mBottom - mTop);
        }
    }

    public final void setBottom(int bottom) {
        if (bottom != mBottom) {
            int oldHeight = mBottom - mTop;
            mBottom = bottom;
            sizeChange(mRight - mLeft, mBottom - mTop, mRight - mLeft, oldHeight);
        }
    }

    public void setLeftTopRightBottom(int left, int top, int right, int bottom) {
        setFrame(left, top, right, bottom);
    }

    public void offsetLeftAndRight(int offset) {
        if (offset != 0) {
            mLeft += offset;
            mRight += offset;
            invalidateParentForChange();
        }
    }

    public void offsetTopAndBottom(int offset) {
        if (offset != 0) {
            mTop += offset;
            mBottom += offset;
            invalidateParentForChange();
        }
    }

    public float getX() {
        return mLeft + mTranslationX;
    }

    public void setX(float x) {
        setTranslationX(x - mLeft);
    }

    public float getY() {
        return mTop + mTranslationY;
    }

    public void setY(float y) {
        setTranslationY(y - mTop);
    }

    public float getZ() {
        return mElevation + mTranslationZ;
    }

    public void setZ(float z) {
        setTranslationZ(z - mElevation);
    }

    public void getHitRect(Rect outRect) {
        if (hasIdentityMatrix()) {
            outRect.set(mLeft, mTop, mRight, mBottom);
        } else {
            RectF r = new RectF(0, 0, getWidth(), getHeight());
            getMatrix().mapRect(r);
            outRect.set((int) r.left + mLeft, (int) r.top + mTop, (int) r.right + mLeft, (int) r.bottom + mTop);
        }
    }

    public void getDrawingRect(Rect outRect) {
        outRect.set(mScrollX, mScrollY, mScrollX + getWidth(), mScrollY + getHeight());
    }

    public void getLocationInWindow(int[] outLocation) {
        if (outLocation == null || outLocation.length < 2) {
            throw new IllegalArgumentException("outLocation must be an array of two integers");
        }
        float[] position = {0, 0};
        transformToWindow(position);
        outLocation[0] = (int) (position[0] + 0.5f);
        outLocation[1] = (int) (position[1] + 0.5f);
    }

    /** Maps a point in this view's coordinates to window coordinates. */
    void transformToWindow(float[] position) {
        if (!hasIdentityMatrix()) getMatrix().mapPoints(position);
        position[0] += mLeft;
        position[1] += mTop;
        ViewParent parent = mParent;
        while (parent instanceof View) {
            final View view = (View) parent;
            position[0] -= view.mScrollX;
            position[1] -= view.mScrollY;
            if (!view.hasIdentityMatrix()) view.getMatrix().mapPoints(position);
            position[0] += view.mLeft;
            position[1] += view.mTop;
            parent = view.mParent;
        }
    }

    public void getLocationOnScreen(int[] outLocation) {
        getLocationInWindow(outLocation);
        if (mAttachInfo != null) {
            Rect frame = mAttachInfo.mViewRootImpl.getWindowFrame();
            outLocation[0] += frame.left;
            outLocation[1] += frame.top;
        }
    }

    public boolean getGlobalVisibleRect(Rect r, android.graphics.Point globalOffset) {
        int width = mRight - mLeft;
        int height = mBottom - mTop;
        if (width > 0 && height > 0) {
            r.set(0, 0, width, height);
            if (globalOffset != null) globalOffset.set(-mScrollX, -mScrollY);
            return mParent == null || mParent.getChildVisibleRect(this, r, globalOffset);
        }
        return false;
    }

    public final boolean getGlobalVisibleRect(Rect r) {
        return getGlobalVisibleRect(r, null);
    }

    public final boolean getLocalVisibleRect(Rect r) {
        android.graphics.Point offset = new android.graphics.Point();
        if (getGlobalVisibleRect(r, offset)) {
            r.offset(-offset.x, -offset.y);
            return true;
        }
        return false;
    }

    public boolean requestRectangleOnScreen(Rect rectangle) {
        return requestRectangleOnScreen(rectangle, false);
    }

    public boolean requestRectangleOnScreen(Rect rectangle, boolean immediate) {
        if (mParent == null) return false;
        View child = this;
        RectF position = new RectF(rectangle);
        boolean scrolled = false;
        ViewParent parent = mParent;
        while (parent != null) {
            Rect r = new Rect((int) position.left, (int) position.top, (int) position.right, (int) position.bottom);
            scrolled |= parent.requestChildRectangleOnScreen(child, r, immediate);
            if (!(parent instanceof View)) break;
            position.offset(child.mLeft - child.mScrollX, child.mTop - child.mScrollY);
            child = (View) parent;
            parent = child.getParent();
        }
        return scrolled;
    }

    public void setClipBounds(Rect clipBounds) {
        mClipBounds = clipBounds != null ? new Rect(clipBounds) : null;
        invalidate();
    }

    public Rect getClipBounds() {
        return mClipBounds != null ? new Rect(mClipBounds) : null;
    }

    public boolean getClipBounds(Rect outRect) {
        if (mClipBounds != null) {
            outRect.set(mClipBounds);
            return true;
        }
        return false;
    }

    // --- transformations --------------------------------------------------------------------------

    public Matrix getMatrix() {
        if (mMatrix == null) mMatrix = new Matrix();
        if (mMatrixDirty) {
            mMatrixDirty = false;
            mInverseMatrix = null;
            float px = mPivotExplicitlySet ? mPivotX : getWidth() / 2f;
            float py = mPivotExplicitlySet ? mPivotY : getHeight() / 2f;
            mMatrix.setTranslate(mTranslationX, mTranslationY);
            if (mRotation != 0) mMatrix.preRotate(mRotation, px, py);
            if (mScaleX != 1f || mScaleY != 1f) mMatrix.preScale(mScaleX, mScaleY, px, py);
        }
        return mMatrix;
    }

    Matrix getInverseMatrix() {
        Matrix m = getMatrix();
        if (mInverseMatrix == null) {
            mInverseMatrix = new Matrix();
            m.invert(mInverseMatrix);
        }
        return mInverseMatrix;
    }

    public boolean hasIdentityMatrix() {
        return mTranslationX == 0 && mTranslationY == 0 && mRotation == 0 && mScaleX == 1f && mScaleY == 1f;
    }

    private void transformChanged() {
        mMatrixDirty = true;
        invalidateParentForChange();
    }

    public float getTranslationX() {
        return mTranslationX;
    }

    public void setTranslationX(float translationX) {
        if (translationX != mTranslationX) {
            mTranslationX = translationX;
            transformChanged();
        }
    }

    public float getTranslationY() {
        return mTranslationY;
    }

    public void setTranslationY(float translationY) {
        if (translationY != mTranslationY) {
            mTranslationY = translationY;
            transformChanged();
        }
    }

    public float getTranslationZ() {
        return mTranslationZ;
    }

    public void setTranslationZ(float translationZ) {
        mTranslationZ = translationZ;
    }

    public float getElevation() {
        return mElevation;
    }

    public void setElevation(float elevation) {
        mElevation = elevation;
    }

    public float getRotation() {
        return mRotation;
    }

    public void setRotation(float rotation) {
        if (rotation != mRotation) {
            mRotation = rotation;
            transformChanged();
        }
    }

    public float getRotationX() {
        return mRotationX;
    }

    public void setRotationX(float rotationX) {
        mRotationX = rotationX;
    }

    public float getRotationY() {
        return mRotationY;
    }

    public void setRotationY(float rotationY) {
        mRotationY = rotationY;
    }

    public float getScaleX() {
        return mScaleX;
    }

    public void setScaleX(float scaleX) {
        if (scaleX != mScaleX) {
            mScaleX = scaleX;
            transformChanged();
        }
    }

    public float getScaleY() {
        return mScaleY;
    }

    public void setScaleY(float scaleY) {
        if (scaleY != mScaleY) {
            mScaleY = scaleY;
            transformChanged();
        }
    }

    public float getPivotX() {
        return mPivotExplicitlySet ? mPivotX : getWidth() / 2f;
    }

    public void setPivotX(float pivotX) {
        mPivotX = pivotX;
        mPivotExplicitlySet = true;
        transformChanged();
    }

    public float getPivotY() {
        return mPivotExplicitlySet ? mPivotY : getHeight() / 2f;
    }

    public void setPivotY(float pivotY) {
        mPivotY = pivotY;
        mPivotExplicitlySet = true;
        transformChanged();
    }

    public boolean isPivotSet() {
        return mPivotExplicitlySet;
    }

    public void resetPivot() {
        mPivotExplicitlySet = false;
        transformChanged();
    }

    public float getCameraDistance() {
        return mCameraDistance;
    }

    public void setCameraDistance(float distance) {
        mCameraDistance = distance;
    }

    public float getAlpha() {
        return mAlpha;
    }

    public void setAlpha(float alpha) {
        if (mAlpha != alpha) {
            mAlpha = alpha;
            invalidateParentForChange();
        }
    }

    public boolean hasOverlappingRendering() {
        return true;
    }

    public void setTransitionAlpha(float alpha) {
        mTransitionAlpha = alpha;
        invalidateParentForChange();
    }

    public float getTransitionAlpha() {
        return mTransitionAlpha;
    }

    float getFinalAlpha() {
        return mAlpha * mTransitionAlpha;
    }

    public void setLayerType(int layerType, Paint paint) {
        mLayerType = layerType;
    }

    public int getLayerType() {
        return mLayerType;
    }

    public void setLayerPaint(Paint paint) {}

    public void buildLayer() {}

    public ViewOutlineProvider getOutlineProvider() {
        return mOutlineProvider;
    }

    public void setOutlineProvider(ViewOutlineProvider provider) {
        mOutlineProvider = provider;
        invalidateOutline();
    }

    public void invalidateOutline() {}

    public final boolean getClipToOutline() {
        return mClipToOutline;
    }

    public void setClipToOutline(boolean clipToOutline) {
        mClipToOutline = clipToOutline;
        invalidate();
    }

    public void setOutlineAmbientShadowColor(int color) {}

    public void setOutlineSpotShadowColor(int color) {}

    public ViewPropertyAnimator animate() {
        if (mAnimator == null) mAnimator = new ViewPropertyAnimator(this);
        return mAnimator;
    }

    // --- view animations --------------------------------------------------------------------------

    public void startAnimation(Animation animation) {
        animation.setStartTime(Animation.START_ON_FIRST_FRAME);
        setAnimation(animation);
        invalidateParentForChange();
    }

    public void clearAnimation() {
        if (mCurrentAnimation != null) mCurrentAnimation.detach();
        mCurrentAnimation = null;
        invalidateParentForChange();
    }

    public void setAnimation(Animation animation) {
        mCurrentAnimation = animation;
        if (animation != null) animation.reset();
    }

    public Animation getAnimation() {
        return mCurrentAnimation;
    }

    protected void onAnimationStart() {}

    protected void onAnimationEnd() {}

    /**
     * Applies the current view animation for drawing at `drawingTime`; returns
     * the transformation to draw with, or null. Keeps frames coming while the
     * animation runs.
     */
    Transformation applyAnimation(long drawingTime, ViewGroup parent) {
        final Animation a = mCurrentAnimation;
        if (a == null) return null;
        if (!a.isInitialized()) {
            a.initialize(getWidth(), getHeight(), parent != null ? parent.getWidth() : 0,
                    parent != null ? parent.getHeight() : 0);
            onAnimationStart();
        }
        mAnimationTransformation.clear();
        boolean more = a.getTransformation(drawingTime, mAnimationTransformation, 1f);
        if (more) {
            if (parent != null) parent.invalidate(); else invalidate();
        } else {
            if (!a.getFillAfter()) {
                mCurrentAnimation = null;
            }
            // onAnimationEnd runs after this frame, as with AOSP's finishAnimatingView.
            final Animation ended = a;
            post(new Runnable() {
                public void run() {
                    if (mCurrentAnimation == null || mCurrentAnimation == ended) onAnimationEnd();
                }
            });
            if (!a.getFillAfter()) return null;
        }
        return mAnimationTransformation;
    }

    // --- scrolling ------------------------------------------------------------------------------------

    public final int getScrollX() {
        return mScrollX;
    }

    public final int getScrollY() {
        return mScrollY;
    }

    public void setScrollX(int value) {
        scrollTo(value, mScrollY);
    }

    public void setScrollY(int value) {
        scrollTo(mScrollX, value);
    }

    public void scrollTo(int x, int y) {
        if (mScrollX != x || mScrollY != y) {
            int oldX = mScrollX;
            int oldY = mScrollY;
            mScrollX = x;
            mScrollY = y;
            onScrollChanged(mScrollX, mScrollY, oldX, oldY);
            if (!awakenScrollBars()) invalidate();
        }
    }

    public void scrollBy(int x, int y) {
        scrollTo(mScrollX + x, mScrollY + y);
    }

    protected void onScrollChanged(int l, int t, int oldl, int oldt) {
        if (mAttachInfo != null) mAttachInfo.mTreeObserver.dispatchOnScrollChanged();
        if (mOnScrollChangeListener != null) mOnScrollChangeListener.onScrollChange(this, l, t, oldl, oldt);
    }

    public void setOnScrollChangeListener(OnScrollChangeListener l) {
        mOnScrollChangeListener = l;
    }

    public void computeScroll() {}

    public boolean canScrollVertically(int direction) {
        final int offset = computeVerticalScrollOffset();
        final int range = computeVerticalScrollRange() - computeVerticalScrollExtent();
        if (range == 0) return false;
        if (direction < 0) return offset > 0;
        return offset < range - 1;
    }

    public boolean canScrollHorizontally(int direction) {
        final int offset = computeHorizontalScrollOffset();
        final int range = computeHorizontalScrollRange() - computeHorizontalScrollExtent();
        if (range == 0) return false;
        if (direction < 0) return offset > 0;
        return offset < range - 1;
    }

    protected int computeHorizontalScrollRange() {
        return getWidth();
    }

    protected int computeHorizontalScrollOffset() {
        return mScrollX;
    }

    protected int computeHorizontalScrollExtent() {
        return getWidth();
    }

    protected int computeVerticalScrollRange() {
        return getHeight();
    }

    protected int computeVerticalScrollOffset() {
        return mScrollY;
    }

    protected int computeVerticalScrollExtent() {
        return getHeight();
    }

    protected boolean overScrollBy(int deltaX, int deltaY, int scrollX, int scrollY, int scrollRangeX,
            int scrollRangeY, int maxOverScrollX, int maxOverScrollY, boolean isTouchEvent) {
        final int overScrollMode = mOverScrollMode;
        final boolean canScrollHorizontal = computeHorizontalScrollRange() > computeHorizontalScrollExtent();
        final boolean canScrollVertical = computeVerticalScrollRange() > computeVerticalScrollExtent();
        final boolean overScrollHorizontal = overScrollMode == OVER_SCROLL_ALWAYS
                || (overScrollMode == OVER_SCROLL_IF_CONTENT_SCROLLS && canScrollHorizontal);
        final boolean overScrollVertical = overScrollMode == OVER_SCROLL_ALWAYS
                || (overScrollMode == OVER_SCROLL_IF_CONTENT_SCROLLS && canScrollVertical);
        int newScrollX = scrollX + deltaX;
        if (!overScrollHorizontal) maxOverScrollX = 0;
        int newScrollY = scrollY + deltaY;
        if (!overScrollVertical) maxOverScrollY = 0;
        final int left = -maxOverScrollX;
        final int right = maxOverScrollX + scrollRangeX;
        final int top = -maxOverScrollY;
        final int bottom = maxOverScrollY + scrollRangeY;
        boolean clampedX = false;
        if (newScrollX > right) {
            newScrollX = right;
            clampedX = true;
        } else if (newScrollX < left) {
            newScrollX = left;
            clampedX = true;
        }
        boolean clampedY = false;
        if (newScrollY > bottom) {
            newScrollY = bottom;
            clampedY = true;
        } else if (newScrollY < top) {
            newScrollY = top;
            clampedY = true;
        }
        onOverScrolled(newScrollX, newScrollY, clampedX, clampedY);
        return clampedX || clampedY;
    }

    protected void onOverScrolled(int scrollX, int scrollY, boolean clampedX, boolean clampedY) {}

    public int getOverScrollMode() {
        return mOverScrollMode;
    }

    public void setOverScrollMode(int overScrollMode) {
        mOverScrollMode = overScrollMode;
    }

    public void setNestedScrollingEnabled(boolean enabled) {
        mNestedScrollingEnabled = enabled;
    }

    public boolean isNestedScrollingEnabled() {
        return mNestedScrollingEnabled;
    }

    public boolean startNestedScroll(int axes) {
        return false;
    }

    public void stopNestedScroll() {}

    public boolean hasNestedScrollingParent() {
        return false;
    }

    public boolean dispatchNestedScroll(int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed,
            int[] offsetInWindow) {
        return false;
    }

    public boolean dispatchNestedPreScroll(int dx, int dy, int[] consumed, int[] offsetInWindow) {
        return false;
    }

    public boolean dispatchNestedFling(float velocityX, float velocityY, boolean consumed) {
        return false;
    }

    public boolean dispatchNestedPreFling(float velocityX, float velocityY) {
        return false;
    }

    public boolean isVerticalScrollBarEnabled() {
        return mVerticalScrollBarEnabled;
    }

    public void setVerticalScrollBarEnabled(boolean verticalScrollBarEnabled) {
        mVerticalScrollBarEnabled = verticalScrollBarEnabled;
    }

    public boolean isHorizontalScrollBarEnabled() {
        return mHorizontalScrollBarEnabled;
    }

    public void setHorizontalScrollBarEnabled(boolean horizontalScrollBarEnabled) {
        mHorizontalScrollBarEnabled = horizontalScrollBarEnabled;
    }

    public void setScrollbarFadingEnabled(boolean fadeScrollbars) {
        mScrollbarFadingEnabled = fadeScrollbars;
    }

    public boolean isScrollbarFadingEnabled() {
        return mScrollbarFadingEnabled;
    }

    public void setScrollBarDefaultDelayBeforeFade(int scrollBarDefaultDelayBeforeFade) {
        mScrollBarDefaultDelayBeforeFade = scrollBarDefaultDelayBeforeFade;
    }

    public int getScrollBarDefaultDelayBeforeFade() {
        return mScrollBarDefaultDelayBeforeFade;
    }

    public void setScrollBarFadeDuration(int scrollBarFadeDuration) {
        mScrollBarFadeDuration = scrollBarFadeDuration;
    }

    public int getScrollBarFadeDuration() {
        return mScrollBarFadeDuration;
    }

    public void setScrollBarStyle(int style) {
        mScrollBarStyle = style;
    }

    public int getScrollBarStyle() {
        return mScrollBarStyle;
    }

    public void setScrollBarSize(int scrollBarSize) {}

    public int getScrollBarSize() {
        return ViewConfiguration.get(mContext).getScaledScrollBarSize();
    }

    public void setVerticalScrollbarPosition(int position) {
        mVerticalScrollbarPosition = position;
    }

    public int getVerticalScrollbarPosition() {
        return mVerticalScrollbarPosition;
    }

    public int getVerticalScrollbarWidth() {
        return mVerticalScrollBarEnabled ? getScrollBarSize() : 0;
    }

    protected int getHorizontalScrollbarHeight() {
        return mHorizontalScrollBarEnabled ? getScrollBarSize() : 0;
    }

    public void setVerticalFadingEdgeEnabled(boolean verticalFadingEdgeEnabled) {
        mVerticalFadingEdgeEnabled = verticalFadingEdgeEnabled;
    }

    public boolean isVerticalFadingEdgeEnabled() {
        return mVerticalFadingEdgeEnabled;
    }

    public void setHorizontalFadingEdgeEnabled(boolean horizontalFadingEdgeEnabled) {
        mHorizontalFadingEdgeEnabled = horizontalFadingEdgeEnabled;
    }

    public boolean isHorizontalFadingEdgeEnabled() {
        return mHorizontalFadingEdgeEnabled;
    }

    public void setFadingEdgeLength(int length) {
        mFadingEdgeLength = length;
    }

    public int getVerticalFadingEdgeLength() {
        return mVerticalFadingEdgeEnabled ? mFadingEdgeLength : 0;
    }

    public int getHorizontalFadingEdgeLength() {
        return mHorizontalFadingEdgeEnabled ? mFadingEdgeLength : 0;
    }

    public void setScrollIndicators(int indicators) {}

    public int getScrollIndicators() {
        return 0;
    }

    protected boolean awakenScrollBars() {
        return awakenScrollBars(mScrollBarDefaultDelayBeforeFade, true);
    }

    protected boolean awakenScrollBars(int startDelay) {
        return awakenScrollBars(startDelay, true);
    }

    protected boolean awakenScrollBars(int startDelay, boolean invalidate) {
        if (!mVerticalScrollBarEnabled && !mHorizontalScrollBarEnabled) return false;
        mScrollBarFadeStartTime = SystemClock.uptimeMillis() + startDelay;
        if (invalidate) {
            invalidate();
            // One more frame once the fade can begin.
            postInvalidateDelayed(startDelay);
        }
        return true;
    }

    /** Draws thin scroll indicators while they are awake (AOSP ScrollBarDrawable, simplified). */
    protected final void onDrawScrollBars(Canvas canvas) {
        if (!mVerticalScrollBarEnabled && !mHorizontalScrollBarEnabled) return;
        long now = SystemClock.uptimeMillis();
        int alpha = 255;
        if (mScrollbarFadingEnabled) {
            if (mScrollBarFadeStartTime == 0) return;
            long t = now - mScrollBarFadeStartTime;
            if (t >= mScrollBarFadeDuration) return;
            if (t > 0) {
                alpha = (int) (255 * (1f - t / (float) mScrollBarFadeDuration));
                invalidate();
            }
        }
        if (mScrollBarPaint == null) mScrollBarPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mScrollBarPaint.setColor(0x80808080);
        mScrollBarPaint.setAlpha(alpha / 2);
        final int size = getScrollBarSize();
        final int width = getWidth();
        final int height = getHeight();
        if (mVerticalScrollBarEnabled) {
            int range = computeVerticalScrollRange();
            int extent = computeVerticalScrollExtent();
            int offset = computeVerticalScrollOffset();
            if (range > extent && extent > 0) {
                int length = Math.max(size * 4, height * extent / range);
                int top = (int) ((long) (height - length) * offset / Math.max(1, range - extent));
                int left = mScrollX + width - size - 2;
                canvas.drawRect(left, mScrollY + top, left + size, mScrollY + top + length, mScrollBarPaint);
            }
        }
        if (mHorizontalScrollBarEnabled) {
            int range = computeHorizontalScrollRange();
            int extent = computeHorizontalScrollExtent();
            int offset = computeHorizontalScrollOffset();
            if (range > extent && extent > 0) {
                int length = Math.max(size * 4, width * extent / range);
                int left = (int) ((long) (width - length) * offset / Math.max(1, range - extent));
                int top = mScrollY + height - size - 2;
                canvas.drawRect(mScrollX + left, top, mScrollX + left + length, top + size, mScrollBarPaint);
            }
        }
    }

    // --- layout params, measure, layout ----------------------------------------------------------

    public ViewGroup.LayoutParams getLayoutParams() {
        return mLayoutParams;
    }

    public void setLayoutParams(ViewGroup.LayoutParams params) {
        if (params == null) throw new NullPointerException("Layout parameters cannot be null");
        mLayoutParams = params;
        if (mParent instanceof ViewGroup) ((ViewGroup) mParent).onSetLayoutParams(this, params);
        requestLayout();
    }

    public final void measure(int widthMeasureSpec, int heightMeasureSpec) {
        final boolean specChanged = widthMeasureSpec != mOldWidthMeasureSpec
                || heightMeasureSpec != mOldHeightMeasureSpec;
        if (mForceLayout || specChanged) {
            mMeasuredDimensionSet = false;
            onMeasure(widthMeasureSpec, heightMeasureSpec);
            if (!mMeasuredDimensionSet) {
                throw new IllegalStateException(getClass().getName()
                        + "#onMeasure() did not set the measured dimension by calling setMeasuredDimension()");
            }
            mLayoutRequired = true;
        }
        mOldWidthMeasureSpec = widthMeasureSpec;
        mOldHeightMeasureSpec = heightMeasureSpec;
    }

    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(getDefaultSize(getSuggestedMinimumWidth(), widthMeasureSpec),
                getDefaultSize(getSuggestedMinimumHeight(), heightMeasureSpec));
    }

    protected final void setMeasuredDimension(int measuredWidth, int measuredHeight) {
        mMeasuredWidth = measuredWidth;
        mMeasuredHeight = measuredHeight;
        mMeasuredDimensionSet = true;
    }

    public final int getMeasuredWidth() {
        return mMeasuredWidth & MEASURED_SIZE_MASK;
    }

    public final int getMeasuredHeight() {
        return mMeasuredHeight & MEASURED_SIZE_MASK;
    }

    public final int getMeasuredWidthAndState() {
        return mMeasuredWidth;
    }

    public final int getMeasuredHeightAndState() {
        return mMeasuredHeight;
    }

    public final int getMeasuredState() {
        return (mMeasuredWidth & MEASURED_STATE_MASK)
                | ((mMeasuredHeight >> MEASURED_HEIGHT_STATE_SHIFT) & (MEASURED_STATE_MASK >> MEASURED_HEIGHT_STATE_SHIFT));
    }

    protected int getSuggestedMinimumWidth() {
        return (mBackground == null) ? mMinWidth : Math.max(mMinWidth, mBackground.getMinimumWidth());
    }

    protected int getSuggestedMinimumHeight() {
        return (mBackground == null) ? mMinHeight : Math.max(mMinHeight, mBackground.getMinimumHeight());
    }

    public int getMinimumWidth() {
        return mMinWidth;
    }

    public int getMinimumHeight() {
        return mMinHeight;
    }

    public void setMinimumWidth(int minWidth) {
        mMinWidth = minWidth;
        requestLayout();
    }

    public void setMinimumHeight(int minHeight) {
        mMinHeight = minHeight;
        requestLayout();
    }

    public int getBaseline() {
        return -1;
    }

    public static int combineMeasuredStates(int curState, int newState) {
        return curState | newState;
    }

    public static int resolveSize(int size, int measureSpec) {
        return resolveSizeAndState(size, measureSpec, 0) & MEASURED_SIZE_MASK;
    }

    public static int resolveSizeAndState(int size, int measureSpec, int childMeasuredState) {
        final int specMode = MeasureSpec.getMode(measureSpec);
        final int specSize = MeasureSpec.getSize(measureSpec);
        final int result;
        switch (specMode) {
            case MeasureSpec.AT_MOST:
                result = specSize < size ? (specSize | MEASURED_STATE_TOO_SMALL) : size;
                break;
            case MeasureSpec.EXACTLY:
                result = specSize;
                break;
            case MeasureSpec.UNSPECIFIED:
            default:
                result = size;
        }
        return result | (childMeasuredState & MEASURED_STATE_MASK);
    }

    public static int getDefaultSize(int size, int measureSpec) {
        int result = size;
        int specMode = MeasureSpec.getMode(measureSpec);
        int specSize = MeasureSpec.getSize(measureSpec);
        switch (specMode) {
            case MeasureSpec.UNSPECIFIED:
                result = size;
                break;
            case MeasureSpec.AT_MOST:
            case MeasureSpec.EXACTLY:
                result = specSize;
                break;
        }
        return result;
    }

    public void layout(int l, int t, int r, int b) {
        int oldL = mLeft, oldT = mTop, oldR = mRight, oldB = mBottom;
        boolean changed = setFrame(l, t, r, b);
        if (changed || mLayoutRequired) {
            onLayout(changed, l, t, r, b);
            mLayoutRequired = false;
            if (mLayoutChangeListeners != null) {
                for (OnLayoutChangeListener li : new ArrayList<OnLayoutChangeListener>(mLayoutChangeListeners)) {
                    li.onLayoutChange(this, l, t, r, b, oldL, oldT, oldR, oldB);
                }
            }
        }
        mForceLayout = false;
        mIsLaidOut = true;
    }

    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {}

    protected boolean setFrame(int left, int top, int right, int bottom) {
        if (mLeft == left && mRight == right && mTop == top && mBottom == bottom) return false;
        int oldWidth = mRight - mLeft;
        int oldHeight = mBottom - mTop;
        int newWidth = right - left;
        int newHeight = bottom - top;
        invalidate();
        mLeft = left;
        mTop = top;
        mRight = right;
        mBottom = bottom;
        if (newWidth != oldWidth || newHeight != oldHeight) sizeChange(newWidth, newHeight, oldWidth, oldHeight);
        invalidate();
        return true;
    }

    private void sizeChange(int newWidth, int newHeight, int oldWidth, int oldHeight) {
        mMatrixDirty = true;
        mBackgroundSizeChanged = true;
        onSizeChanged(newWidth, newHeight, oldWidth, oldHeight);
        invalidateOutline();
    }

    protected void onSizeChanged(int w, int h, int oldw, int oldh) {}

    public void requestLayout() {
        mForceLayout = true;
        mLayoutRequired = true;
        if (mParent != null && !mParent.isLayoutRequested()) mParent.requestLayout();
    }

    public void forceLayout() {
        mForceLayout = true;
        mLayoutRequired = true;
    }

    public boolean isLayoutRequested() {
        return mForceLayout;
    }

    public boolean isInLayout() {
        return mAttachInfo != null && mAttachInfo.mViewRootImpl.isInLayout();
    }

    // --- drawing --------------------------------------------------------------------------------

    public void invalidate() {
        if (mAttachInfo != null) mAttachInfo.mViewRootImpl.invalidate();
    }

    public void invalidate(boolean invalidateCache) {
        invalidate();
    }

    public void invalidate(Rect dirty) {
        invalidate();
    }

    public void invalidate(int l, int t, int r, int b) {
        invalidate();
    }

    void invalidateParentForChange() {
        invalidate();
    }

    protected void invalidateParentCaches() {}

    protected void invalidateParentIfNeeded() {
        invalidate();
    }

    public void setWillNotDraw(boolean willNotDraw) {
        mWillNotDraw = willNotDraw;
    }

    public boolean willNotDraw() {
        return mWillNotDraw;
    }

    public void setWillNotCacheDrawing(boolean willNotCacheDrawing) {}

    public boolean willNotCacheDrawing() {
        return false;
    }

    public long getDrawingTime() {
        return mAttachInfo != null ? mAttachInfo.mDrawingTime : SystemClock.uptimeMillis();
    }

    public void draw(Canvas canvas) {
        drawBackground(canvas);
        onDraw(canvas);
        dispatchDraw(canvas);
        onDrawForeground(canvas);
    }

    /** AOSP's PFLAG_SKIP_DRAW: a will-not-draw view without drawables only dispatches to children. */
    boolean skipsDraw() {
        return mWillNotDraw && mBackground == null && mForeground == null;
    }

    private void drawBackground(Canvas canvas) {
        final Drawable background = mBackground;
        if (background == null) return;
        if (mBackgroundSizeChanged) {
            background.setBounds(0, 0, mRight - mLeft, mBottom - mTop);
            mBackgroundSizeChanged = false;
        }
        final int scrollX = mScrollX;
        final int scrollY = mScrollY;
        if ((scrollX | scrollY) == 0) {
            background.draw(canvas);
        } else {
            canvas.translate(scrollX, scrollY);
            background.draw(canvas);
            canvas.translate(-scrollX, -scrollY);
        }
    }

    protected void onDraw(Canvas canvas) {}

    protected void dispatchDraw(Canvas canvas) {}

    public void onDrawForeground(Canvas canvas) {
        onDrawScrollBars(canvas);
        final Drawable foreground = mForeground;
        if (foreground != null) {
            final Rect selfBounds = mForegroundSelfBounds;
            final Rect overlayBounds = mForegroundOverlayBounds;
            selfBounds.set(0, 0, getWidth(), getHeight());
            Gravity.apply(mForegroundGravity, foreground.getIntrinsicWidth(), foreground.getIntrinsicHeight(),
                    selfBounds, overlayBounds, getLayoutDirection());
            foreground.setBounds(overlayBounds);
            if ((mScrollX | mScrollY) != 0) {
                canvas.translate(mScrollX, mScrollY);
                foreground.draw(canvas);
                canvas.translate(-mScrollX, -mScrollY);
            } else {
                foreground.draw(canvas);
            }
        }
    }

    public void setDrawingCacheEnabled(boolean enabled) {
        mDrawingCacheEnabled = enabled;
        if (!enabled) destroyDrawingCache();
    }

    public boolean isDrawingCacheEnabled() {
        return mDrawingCacheEnabled;
    }

    public void buildDrawingCache() {
        buildDrawingCache(false);
    }

    public void buildDrawingCache(boolean autoScale) {
        int width = getWidth();
        int height = getHeight();
        if (width <= 0 || height <= 0) return;
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        if (mDrawingCacheBackgroundColor != 0) canvas.drawColor(mDrawingCacheBackgroundColor);
        canvas.translate(-mScrollX, -mScrollY);
        draw(canvas);
        mDrawingCache = bitmap;
    }

    public Bitmap getDrawingCache() {
        return getDrawingCache(false);
    }

    public Bitmap getDrawingCache(boolean autoScale) {
        if (mDrawingCache == null && mDrawingCacheEnabled) buildDrawingCache(autoScale);
        return mDrawingCache;
    }

    public void destroyDrawingCache() {
        mDrawingCache = null;
    }

    public void setDrawingCacheBackgroundColor(int color) {
        mDrawingCacheBackgroundColor = color;
    }

    public int getDrawingCacheBackgroundColor() {
        return mDrawingCacheBackgroundColor;
    }

    public void setDrawingCacheQuality(int quality) {}

    // --- saved state ---------------------------------------------------------------------------

    public void setSaveEnabled(boolean enabled) {
        mSaveEnabled = enabled;
    }

    public boolean isSaveEnabled() {
        return mSaveEnabled;
    }

    public void setSaveFromParentEnabled(boolean enabled) {}

    public boolean isSaveFromParentEnabled() {
        return true;
    }

    public void saveHierarchyState(SparseArray<Parcelable> container) {
        dispatchSaveInstanceState(container);
    }

    protected void dispatchSaveInstanceState(SparseArray<Parcelable> container) {
        if (mId != NO_ID && mSaveEnabled) {
            Parcelable state = onSaveInstanceState();
            if (state != null) container.put(mId, state);
        }
    }

    protected Parcelable onSaveInstanceState() {
        return BaseSavedState.EMPTY_STATE;
    }

    public void restoreHierarchyState(SparseArray<Parcelable> container) {
        dispatchRestoreInstanceState(container);
    }

    protected void dispatchRestoreInstanceState(SparseArray<Parcelable> container) {
        if (mId != NO_ID) {
            Parcelable state = container.get(mId);
            if (state != null) onRestoreInstanceState(state);
        }
    }

    protected void onRestoreInstanceState(Parcelable state) {
        if (state != null && !(state instanceof AbsSavedState)) {
            throw new IllegalArgumentException("Wrong state class, expecting View State but received "
                    + state.getClass().toString() + " instead.");
        }
    }

    // --- window-related flags ----------------------------------------------------------------------

    public void setKeepScreenOn(boolean keepScreenOn) {
        mKeepScreenOn = keepScreenOn;
    }

    public boolean getKeepScreenOn() {
        return mKeepScreenOn;
    }

    public void setFitsSystemWindows(boolean fitSystemWindows) {
        mFitsSystemWindows = fitSystemWindows;
    }

    public boolean getFitsSystemWindows() {
        return mFitsSystemWindows;
    }

    public boolean fitsSystemWindows() {
        return mFitsSystemWindows;
    }

    public void requestFitSystemWindows() {
        requestApplyInsets();
    }

    public void requestApplyInsets() {
        if (mAttachInfo != null) {
            final View root = this;
            post(new Runnable() {
                public void run() {
                    root.getRootView().dispatchApplyWindowInsets(WindowInsets.none());
                }
            });
        }
    }

    public void setOnApplyWindowInsetsListener(OnApplyWindowInsetsListener listener) {
        mOnApplyWindowInsetsListener = listener;
    }

    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        return insets;
    }

    public WindowInsets dispatchApplyWindowInsets(WindowInsets insets) {
        if (mOnApplyWindowInsetsListener != null) {
            return mOnApplyWindowInsetsListener.onApplyWindowInsets(this, insets);
        }
        return onApplyWindowInsets(insets);
    }

    public WindowInsets getRootWindowInsets() {
        return mAttachInfo != null ? WindowInsets.none() : null;
    }

    public WindowInsets computeSystemWindowInsets(WindowInsets in, Rect outLocalInsets) {
        outLocalInsets.set(0, 0, 0, 0);
        return in;
    }

    public WindowInsetsController getWindowInsetsController() {
        return mAttachInfo != null ? mAttachInfo.mViewRootImpl.getInsetsController() : null;
    }

    public void setSystemUiVisibility(int visibility) {
        if (visibility != mSystemUiVisibility) {
            mSystemUiVisibility = visibility;
            if (mOnSystemUiVisibilityChangeListener != null) {
                final int v = visibility;
                post(new Runnable() {
                    public void run() {
                        if (mOnSystemUiVisibilityChangeListener != null) {
                            mOnSystemUiVisibilityChangeListener.onSystemUiVisibilityChange(v);
                        }
                    }
                });
            }
        }
    }

    public int getSystemUiVisibility() {
        return mSystemUiVisibility;
    }

    public int getWindowSystemUiVisibility() {
        return mSystemUiVisibility;
    }

    public void setOnSystemUiVisibilityChangeListener(OnSystemUiVisibilityChangeListener l) {
        mOnSystemUiVisibilityChangeListener = l;
    }

    // --- feedback and accessibility (no audio or vibration on the desktop host) ---------------

    public void setSoundEffectsEnabled(boolean soundEffectsEnabled) {
        mSoundEffectsEnabled = soundEffectsEnabled;
    }

    public boolean isSoundEffectsEnabled() {
        return mSoundEffectsEnabled;
    }

    public void playSoundEffect(int soundConstant) {}

    public void setHapticFeedbackEnabled(boolean hapticFeedbackEnabled) {
        mHapticFeedbackEnabled = hapticFeedbackEnabled;
    }

    public boolean isHapticFeedbackEnabled() {
        return mHapticFeedbackEnabled;
    }

    public boolean performHapticFeedback(int feedbackConstant) {
        return false;
    }

    public boolean performHapticFeedback(int feedbackConstant, int flags) {
        return false;
    }

    public void setImportantForAccessibility(int mode) {
        mImportantForAccessibility = mode;
    }

    public int getImportantForAccessibility() {
        return mImportantForAccessibility;
    }

    public boolean isImportantForAccessibility() {
        return mImportantForAccessibility != IMPORTANT_FOR_ACCESSIBILITY_NO;
    }

    public void setAccessibilityLiveRegion(int mode) {
        mAccessibilityLiveRegion = mode;
    }

    public int getAccessibilityLiveRegion() {
        return mAccessibilityLiveRegion;
    }

    public void setImportantForAutofill(int mode) {
        mImportantForAutofill = mode;
    }

    public int getImportantForAutofill() {
        return mImportantForAutofill;
    }

    public void setAutofillHints(String... autofillHints) {}

    public String[] getAutofillHints() {
        return null;
    }

    public int getAutofillType() {
        return AUTOFILL_TYPE_NONE;
    }

    public void announceForAccessibility(CharSequence text) {}

    public void sendAccessibilityEvent(int eventType) {}

    public void setAccessibilityDelegate(AccessibilityDelegate delegate) {
        mAccessibilityDelegate = delegate;
    }

    public AccessibilityDelegate getAccessibilityDelegate() {
        return mAccessibilityDelegate;
    }

    public void setAccessibilityHeading(boolean isHeading) {}

    public void setScreenReaderFocusable(boolean screenReaderFocusable) {}

    public CharSequence getAccessibilityClassName() {
        return View.class.getName();
    }

    /** Accessibility hooks; the runner has no accessibility services, so none are called. */
    public static class AccessibilityDelegate {
        public AccessibilityDelegate() {}

        public void sendAccessibilityEvent(View host, int eventType) {}

        public boolean performAccessibilityAction(View host, int action, android.os.Bundle args) {
            return false;
        }
    }

    // --- misc ---------------------------------------------------------------------------------------

    public boolean isInEditMode() {
        return false;
    }

    public boolean isHardwareAccelerated() {
        return false;
    }

    public boolean isTemporarilyDetached() {
        return false;
    }

    public void onStartTemporaryDetach() {}

    public void onFinishTemporaryDetach() {}

    public void bringToFront() {
        if (mParent != null) mParent.bringChildToFront(this);
    }

    public boolean startDragAndDrop(android.content.ClipData data, DragShadowBuilder shadowBuilder, Object myLocalState,
            int flags) {
        return false;
    }

    public static class DragShadowBuilder {
        private final View mView;

        public DragShadowBuilder(View view) {
            mView = view;
        }

        public DragShadowBuilder() {
            mView = null;
        }

        public final View getView() {
            return mView;
        }

        public void onProvideShadowMetrics(android.graphics.Point outShadowSize,
                android.graphics.Point outShadowTouchPoint) {}

        public void onDrawShadow(Canvas canvas) {}
    }

    public boolean onDragEvent(DragEvent event) {
        return false;
    }

    public boolean dispatchDragEvent(DragEvent event) {
        if (mOnDragListener != null && mEnabled && mOnDragListener.onDrag(this, event)) return true;
        return onDragEvent(event);
    }

    @Override
    public String toString() {
        StringBuilder out = new StringBuilder(128);
        out.append(getClass().getName()).append('{').append(Integer.toHexString(System.identityHashCode(this)));
        out.append(' ').append(mVisibility == VISIBLE ? 'V' : mVisibility == INVISIBLE ? 'I' : 'G');
        out.append(isFocusable() ? 'F' : '.').append(mEnabled ? 'E' : '.').append(mWillNotDraw ? '.' : 'D');
        out.append(mHorizontalScrollBarEnabled ? 'H' : '.').append(mVerticalScrollBarEnabled ? 'V' : '.');
        out.append(mClickable ? 'C' : '.').append(mLongClickable ? 'L' : '.');
        out.append(' ').append(mFocused ? 'F' : '.').append(mSelected ? 'S' : '.').append(mPressed ? 'P' : '.');
        out.append(' ').append(mLeft).append(',').append(mTop).append('-').append(mRight).append(',').append(mBottom);
        if (mId != NO_ID && (mId >>> 24) != 0) {
            out.append(" #").append(Integer.toHexString(mId));
            try {
                Resources r = getResources();
                if (r != null) {
                    out.append(' ').append(r.getResourcePackageName(mId)).append(":id/")
                            .append(r.getResourceEntryName(mId));
                }
            } catch (Resources.NotFoundException e) {
                // No name.
            }
        }
        return out.append('}').toString();
    }

    static void log(String msg) {
        Log.d(VIEW_LOG_TAG, msg);
    }
}
