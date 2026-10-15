package android.view;

import android.content.Context;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.content.res.XmlResourceParser;
import android.util.AttributeSet;
import android.util.Xml;
import java.io.IOException;
import java.lang.reflect.Constructor;
import java.util.HashMap;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/**
 * Instantiates layout XML into views: tags name view classes (with the
 * platform's package prefixes), &lt;merge&gt;, &lt;include&gt;, &lt;requestFocus&gt;
 * and &lt;tag&gt; are supported, as are factories and android:theme overrides.
 */
public abstract class LayoutInflater {
    private static final String TAG_MERGE = "merge";
    private static final String TAG_INCLUDE = "include";
    private static final String TAG_1995 = "blink";
    private static final String TAG_REQUEST_FOCUS = "requestFocus";
    private static final String TAG_TAG = "tag";
    private static final String ATTR_LAYOUT = "layout";
    private static final int[] ATTRS_THEME = new int[] {android.R.attr.theme};

    static final Class<?>[] mConstructorSignature = new Class<?>[] {Context.class, AttributeSet.class};
    private static final HashMap<String, Constructor<? extends View>> sConstructorMap =
            new HashMap<String, Constructor<? extends View>>();

    protected final Context mContext;
    private boolean mFactorySet;
    private Factory mFactory;
    private Factory2 mFactory2;
    private Factory2 mPrivateFactory;
    private Filter mFilter;
    final Object[] mConstructorArgs = new Object[2];
    private HashMap<String, Boolean> mFilterMap;

    public interface Filter {
        boolean onLoadClass(Class clazz);
    }

    public interface Factory {
        View onCreateView(String name, Context context, AttributeSet attrs);
    }

    public interface Factory2 extends Factory {
        View onCreateView(View parent, String name, Context context, AttributeSet attrs);
    }

    private static class FactoryMerger implements Factory2 {
        private final Factory mF1;
        private final Factory mF2;
        private final Factory2 mF12;
        private final Factory2 mF22;

        FactoryMerger(Factory f1, Factory2 f12, Factory f2, Factory2 f22) {
            mF1 = f1;
            mF2 = f2;
            mF12 = f12;
            mF22 = f22;
        }

        public View onCreateView(String name, Context context, AttributeSet attrs) {
            View v = mF1.onCreateView(name, context, attrs);
            if (v != null) return v;
            return mF2.onCreateView(name, context, attrs);
        }

        public View onCreateView(View parent, String name, Context context, AttributeSet attrs) {
            View v = mF12 != null ? mF12.onCreateView(parent, name, context, attrs)
                    : mF1.onCreateView(name, context, attrs);
            if (v != null) return v;
            return mF22 != null ? mF22.onCreateView(parent, name, context, attrs)
                    : mF2.onCreateView(name, context, attrs);
        }
    }

    protected LayoutInflater(Context context) {
        mContext = context;
    }

    protected LayoutInflater(LayoutInflater original, Context newContext) {
        mContext = newContext;
        mFactory = original.mFactory;
        mFactory2 = original.mFactory2;
        mPrivateFactory = original.mPrivateFactory;
        setFilter(original.mFilter);
    }

    public static LayoutInflater from(Context context) {
        LayoutInflater layoutInflater = (LayoutInflater) context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        if (layoutInflater == null) throw new AssertionError("LayoutInflater not found.");
        return layoutInflater;
    }

    public abstract LayoutInflater cloneInContext(Context newContext);

    public Context getContext() {
        return mContext;
    }

    public final Factory getFactory() {
        return mFactory;
    }

    public final Factory2 getFactory2() {
        return mFactory2;
    }

    public void setFactory(Factory factory) {
        if (mFactorySet) throw new IllegalStateException("A factory has already been set on this LayoutInflater");
        if (factory == null) throw new NullPointerException("Given factory can not be null");
        mFactorySet = true;
        if (mFactory == null) {
            mFactory = factory;
        } else {
            mFactory = new FactoryMerger(factory, null, mFactory, mFactory2);
        }
    }

    public void setFactory2(Factory2 factory) {
        if (mFactorySet) throw new IllegalStateException("A factory has already been set on this LayoutInflater");
        if (factory == null) throw new NullPointerException("Given factory can not be null");
        mFactorySet = true;
        if (mFactory == null) {
            mFactory = mFactory2 = factory;
        } else {
            mFactory = mFactory2 = new FactoryMerger(factory, factory, mFactory, mFactory2);
        }
    }

    /** The factory the Activity installs for fragments (hidden on Android). */
    public void setPrivateFactory(Factory2 factory) {
        if (mPrivateFactory == null) {
            mPrivateFactory = factory;
        } else {
            mPrivateFactory = new FactoryMerger(factory, factory, mPrivateFactory, mPrivateFactory);
        }
    }

    public Filter getFilter() {
        return mFilter;
    }

    public void setFilter(Filter filter) {
        mFilter = filter;
        if (filter != null) mFilterMap = new HashMap<String, Boolean>();
    }

    public View inflate(int resource, ViewGroup root) {
        return inflate(resource, root, root != null);
    }

    public View inflate(XmlPullParser parser, ViewGroup root) {
        return inflate(parser, root, root != null);
    }

    public View inflate(int resource, ViewGroup root, boolean attachToRoot) {
        final Resources res = getContext().getResources();
        XmlResourceParser parser = res.getLayout(resource);
        try {
            return inflate(parser, root, attachToRoot);
        } finally {
            parser.close();
        }
    }

    private static void advanceToRootNode(XmlPullParser parser) throws InflateException, IOException,
            XmlPullParserException {
        int type;
        while ((type = parser.next()) != XmlPullParser.START_TAG && type != XmlPullParser.END_DOCUMENT) {
            // Empty
        }
        if (type != XmlPullParser.START_TAG) {
            throw new InflateException(parser.getPositionDescription() + ": No start tag found!");
        }
    }

    public View inflate(XmlPullParser parser, ViewGroup root, boolean attachToRoot) {
        synchronized (mConstructorArgs) {
            final Context inflaterContext = mContext;
            final AttributeSet attrs = Xml.asAttributeSet(parser);
            Context lastContext = (Context) mConstructorArgs[0];
            mConstructorArgs[0] = inflaterContext;
            View result = root;
            try {
                advanceToRootNode(parser);
                final String name = parser.getName();
                if (TAG_MERGE.equals(name)) {
                    if (root == null || !attachToRoot) {
                        throw new InflateException("<merge /> can be used only with a valid ViewGroup root and "
                                + "attachToRoot=true");
                    }
                    rInflate(parser, root, inflaterContext, attrs, false);
                } else {
                    final View temp = createViewFromTag(root, name, inflaterContext, attrs);
                    ViewGroup.LayoutParams params = null;
                    if (root != null) {
                        params = root.generateLayoutParams(attrs);
                        if (!attachToRoot) temp.setLayoutParams(params);
                    }
                    rInflateChildren(parser, temp, attrs, true);
                    if (root != null && attachToRoot) root.addView(temp, params);
                    if (root == null || !attachToRoot) result = temp;
                }
            } catch (XmlPullParserException e) {
                final InflateException ie = new InflateException(e.getMessage(), e);
                throw ie;
            } catch (IOException e) {
                final InflateException ie = new InflateException(getParserStateDescription(inflaterContext, attrs)
                        + ": " + e.getMessage(), e);
                throw ie;
            } finally {
                mConstructorArgs[0] = lastContext;
                mConstructorArgs[1] = null;
            }
            return result;
        }
    }

    private static String getParserStateDescription(Context context, AttributeSet attrs) {
        String description = attrs.getPositionDescription();
        return description != null ? description : "Binary XML file";
    }

    private static boolean verifyClassLoader(Constructor<? extends View> constructor, Context context) {
        final ClassLoader constructorLoader = constructor.getDeclaringClass().getClassLoader();
        if (constructorLoader == LayoutInflater.class.getClassLoader()) return true;
        ClassLoader cl = context.getClassLoader();
        do {
            if (constructorLoader == cl) return true;
            cl = cl.getParent();
        } while (cl != null);
        return false;
    }

    public final View createView(String name, String prefix, AttributeSet attrs)
            throws ClassNotFoundException, InflateException {
        Context context = (Context) mConstructorArgs[0];
        if (context == null) context = mContext;
        return createView(context, name, prefix, attrs);
    }

    public final View createView(Context viewContext, String name, String prefix, AttributeSet attrs)
            throws ClassNotFoundException, InflateException {
        if (viewContext == null) throw new NullPointerException("viewContext");
        if (name == null) throw new NullPointerException("name");
        Constructor<? extends View> constructor = sConstructorMap.get(name);
        if (constructor != null && !verifyClassLoader(constructor, viewContext)) {
            constructor = null;
            sConstructorMap.remove(name);
        }
        Class<? extends View> clazz = null;
        try {
            if (constructor == null) {
                clazz = Class.forName(prefix != null ? (prefix + name) : name, false, mContext.getClassLoader())
                        .asSubclass(View.class);
                if (mFilter != null && clazz != null) {
                    boolean allowed = mFilter.onLoadClass(clazz);
                    if (!allowed) failNotAllowed(name, prefix, viewContext, attrs);
                }
                constructor = clazz.getConstructor(mConstructorSignature);
                constructor.setAccessible(true);
                sConstructorMap.put(name, constructor);
            } else if (mFilter != null) {
                Boolean allowedState = mFilterMap.get(name);
                if (allowedState == null) {
                    clazz = Class.forName(prefix != null ? (prefix + name) : name, false, mContext.getClassLoader())
                            .asSubclass(View.class);
                    boolean allowed = clazz != null && mFilter.onLoadClass(clazz);
                    mFilterMap.put(name, allowed);
                    if (!allowed) failNotAllowed(name, prefix, viewContext, attrs);
                } else if (allowedState.equals(Boolean.FALSE)) {
                    failNotAllowed(name, prefix, viewContext, attrs);
                }
            }
            Object lastContext = mConstructorArgs[0];
            mConstructorArgs[0] = viewContext;
            Object[] args = mConstructorArgs;
            args[1] = attrs;
            try {
                final View view = constructor.newInstance(args);
                if (view instanceof ViewStub) {
                    final ViewStub viewStub = (ViewStub) view;
                    viewStub.setLayoutInflater(cloneInContext((Context) args[0]));
                }
                return view;
            } finally {
                mConstructorArgs[0] = lastContext;
            }
        } catch (NoSuchMethodException e) {
            final InflateException ie = new InflateException(getParserStateDescription(viewContext, attrs)
                    + ": Error inflating class " + (prefix != null ? (prefix + name) : name), e);
            throw ie;
        } catch (ClassCastException e) {
            final InflateException ie = new InflateException(getParserStateDescription(viewContext, attrs)
                    + ": Class is not a View " + (prefix != null ? (prefix + name) : name), e);
            throw ie;
        } catch (ClassNotFoundException e) {
            throw e;
        } catch (java.lang.reflect.InvocationTargetException e) {
            Throwable cause = e.getCause() != null ? e.getCause() : e;
            final InflateException ie = new InflateException(getParserStateDescription(viewContext, attrs)
                    + ": Error inflating class " + (clazz == null ? "<unknown>" : clazz.getName()), cause);
            throw ie;
        } catch (Exception e) {
            final InflateException ie = new InflateException(getParserStateDescription(viewContext, attrs)
                    + ": Error inflating class " + (clazz == null ? "<unknown>" : clazz.getName()), e);
            throw ie;
        }
    }

    private void failNotAllowed(String name, String prefix, Context context, AttributeSet attrs) {
        throw new InflateException(getParserStateDescription(context, attrs) + ": Class not allowed to be inflated "
                + (prefix != null ? (prefix + name) : name));
    }

    protected View onCreateView(String name, AttributeSet attrs) throws ClassNotFoundException {
        return createView(name, "android.view.", attrs);
    }

    protected View onCreateView(View parent, String name, AttributeSet attrs) throws ClassNotFoundException {
        return onCreateView(name, attrs);
    }

    public View onCreateView(Context viewContext, View parent, String name, AttributeSet attrs)
            throws ClassNotFoundException {
        return onCreateView(parent, name, attrs);
    }

    private View createViewFromTag(View parent, String name, Context context, AttributeSet attrs) {
        return createViewFromTag(parent, name, context, attrs, false);
    }

    View createViewFromTag(View parent, String name, Context context, AttributeSet attrs, boolean ignoreThemeAttr) {
        if (name.equals("view")) name = attrs.getAttributeValue(null, "class");
        if (!ignoreThemeAttr) {
            final TypedArray ta = context.obtainStyledAttributes(attrs, ATTRS_THEME);
            final int themeResId = ta.getResourceId(0, 0);
            if (themeResId != 0) context = new ContextThemeWrapper(context, themeResId);
            ta.recycle();
        }
        try {
            View view = tryCreateView(parent, name, context, attrs);
            if (view == null) {
                final Object lastContext = mConstructorArgs[0];
                mConstructorArgs[0] = context;
                try {
                    if (-1 == name.indexOf('.')) {
                        view = onCreateView(context, parent, name, attrs);
                    } else {
                        view = createView(context, name, null, attrs);
                    }
                } finally {
                    mConstructorArgs[0] = lastContext;
                }
            }
            return view;
        } catch (InflateException e) {
            throw e;
        } catch (ClassNotFoundException e) {
            final InflateException ie = new InflateException(getParserStateDescription(context, attrs)
                    + ": Error inflating class " + name, e);
            throw ie;
        } catch (Exception e) {
            final InflateException ie = new InflateException(getParserStateDescription(context, attrs)
                    + ": Error inflating class " + name, e);
            throw ie;
        }
    }

    public final View tryCreateView(View parent, String name, Context context, AttributeSet attrs) {
        if (name.equals(TAG_1995)) {
            throw new InflateException(getParserStateDescription(context, attrs)
                    + ": <blink> is not supported by this runtime");
        }
        View view;
        if (mFactory2 != null) {
            view = mFactory2.onCreateView(parent, name, context, attrs);
        } else if (mFactory != null) {
            view = mFactory.onCreateView(name, context, attrs);
        } else {
            view = null;
        }
        if (view == null && mPrivateFactory != null) view = mPrivateFactory.onCreateView(parent, name, context, attrs);
        return view;
    }

    final void rInflateChildren(XmlPullParser parser, View parent, AttributeSet attrs, boolean finishInflate)
            throws XmlPullParserException, IOException {
        rInflate(parser, parent, parent.getContext(), attrs, finishInflate);
    }

    void rInflate(XmlPullParser parser, View parent, Context context, AttributeSet attrs, boolean finishInflate)
            throws XmlPullParserException, IOException {
        final int depth = parser.getDepth();
        int type;
        boolean pendingRequestFocus = false;
        while (((type = parser.next()) != XmlPullParser.END_TAG || parser.getDepth() > depth)
                && type != XmlPullParser.END_DOCUMENT) {
            if (type != XmlPullParser.START_TAG) continue;
            final String name = parser.getName();
            if (TAG_REQUEST_FOCUS.equals(name)) {
                pendingRequestFocus = true;
                consumeChildElements(parser);
            } else if (TAG_TAG.equals(name)) {
                parseViewTag(parser, parent, attrs);
            } else if (TAG_INCLUDE.equals(name)) {
                if (parser.getDepth() == 0) throw new InflateException("<include /> cannot be the root element");
                parseInclude(parser, context, parent, attrs);
            } else if (TAG_MERGE.equals(name)) {
                throw new InflateException("<merge /> must be the root element");
            } else {
                final View view = createViewFromTag(parent, name, context, attrs);
                final ViewGroup viewGroup = (ViewGroup) parent;
                final ViewGroup.LayoutParams params = viewGroup.generateLayoutParams(attrs);
                rInflateChildren(parser, view, attrs, true);
                viewGroup.addView(view, params);
            }
        }
        if (pendingRequestFocus) parent.restoreDefaultFocusAfterInflate();
        if (finishInflate) parent.onFinishInflate();
    }

    private void parseViewTag(XmlPullParser parser, View view, AttributeSet attrs)
            throws XmlPullParserException, IOException {
        final Context context = view.getContext();
        final TypedArray ta = context.obtainStyledAttributes(attrs, android.R.styleable.ViewTag);
        final int key = ta.getResourceId(android.R.styleable.ViewTag_id, 0);
        final CharSequence value = ta.getText(android.R.styleable.ViewTag_value);
        view.setTag(key, value);
        ta.recycle();
        consumeChildElements(parser);
    }

    private void parseInclude(XmlPullParser parser, Context context, View parent, AttributeSet attrs)
            throws XmlPullParserException, IOException {
        int type;
        if (!(parent instanceof ViewGroup)) {
            throw new InflateException("<include /> can only be used inside of a ViewGroup");
        }
        final TypedArray ta = context.obtainStyledAttributes(attrs, ATTRS_THEME);
        final int themeResId = ta.getResourceId(0, 0);
        final boolean hasThemeOverride = themeResId != 0;
        if (hasThemeOverride) context = new ContextThemeWrapper(context, themeResId);
        ta.recycle();

        int layout = attrs.getAttributeResourceValue(null, ATTR_LAYOUT, 0);
        if (layout == 0) {
            final String value = attrs.getAttributeValue(null, ATTR_LAYOUT);
            if (value == null || value.length() <= 0) {
                throw new InflateException("You must specify a layout in the include tag: "
                        + "<include layout=\"@layout/layoutID\" />");
            }
            layout = context.getResources().getIdentifier(value.substring(1), "attr", context.getPackageName());
        }
        if (layout != 0 && (layout >>> 24) != 0) {
            android.util.TypedValue tv = new android.util.TypedValue();
            if (context.getResources().getResourceTypeName(layout).equals("attr")
                    && context.getTheme().resolveAttribute(layout, tv, true)) {
                layout = tv.resourceId;
            }
        }
        if (layout == 0) {
            final String value = attrs.getAttributeValue(null, ATTR_LAYOUT);
            throw new InflateException("You must specify a valid layout reference. The layout ID " + value
                    + " is not valid.");
        }
        final XmlResourceParser childParser = context.getResources().getLayout(layout);
        try {
            final AttributeSet childAttrs = Xml.asAttributeSet(childParser);
            while ((type = childParser.next()) != XmlPullParser.START_TAG && type != XmlPullParser.END_DOCUMENT) {
                // Empty.
            }
            if (type != XmlPullParser.START_TAG) {
                throw new InflateException(getParserStateDescription(context, childAttrs) + ": No start tag found!");
            }
            final String childName = childParser.getName();
            if (TAG_MERGE.equals(childName)) {
                rInflate(childParser, parent, context, childAttrs, false);
            } else {
                final View view = createViewFromTag(parent, childName, context, childAttrs, hasThemeOverride);
                final ViewGroup group = (ViewGroup) parent;
                final TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.Include);
                final int id = a.getResourceId(android.R.styleable.Include_id, View.NO_ID);
                final int visibility = a.getInt(android.R.styleable.Include_visibility, -1);
                a.recycle();
                ViewGroup.LayoutParams params = null;
                try {
                    params = group.generateLayoutParams(attrs);
                } catch (RuntimeException e) {
                    // The include tag has no layout_width/height; use the included root's.
                }
                if (params == null) params = group.generateLayoutParams(childAttrs);
                view.setLayoutParams(params);
                rInflateChildren(childParser, view, childAttrs, true);
                if (id != View.NO_ID) view.setId(id);
                switch (visibility) {
                    case 0:
                        view.setVisibility(View.VISIBLE);
                        break;
                    case 1:
                        view.setVisibility(View.INVISIBLE);
                        break;
                    case 2:
                        view.setVisibility(View.GONE);
                        break;
                    default:
                        break;
                }
                group.addView(view);
            }
        } finally {
            childParser.close();
        }
        consumeChildElements(parser);
    }

    static final void consumeChildElements(XmlPullParser parser) throws XmlPullParserException, IOException {
        int type;
        final int currentDepth = parser.getDepth();
        while (((type = parser.next()) != XmlPullParser.END_TAG || parser.getDepth() > currentDepth)
                && type != XmlPullParser.END_DOCUMENT) {
            // Empty
        }
    }
}
