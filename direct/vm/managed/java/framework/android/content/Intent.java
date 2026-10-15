package android.content;

import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Locale;
import java.util.Objects;
import java.util.Set;

/** A description of an operation to perform: an action on data, or an explicit component. */
public class Intent implements Cloneable, Parcelable {
    public static final String ACTION_MAIN = "android.intent.action.MAIN";
    public static final String ACTION_VIEW = "android.intent.action.VIEW";
    public static final String ACTION_DEFAULT = ACTION_VIEW;
    public static final String ACTION_EDIT = "android.intent.action.EDIT";
    public static final String ACTION_INSERT = "android.intent.action.INSERT";
    public static final String ACTION_DELETE = "android.intent.action.DELETE";
    public static final String ACTION_PICK = "android.intent.action.PICK";
    public static final String ACTION_GET_CONTENT = "android.intent.action.GET_CONTENT";
    public static final String ACTION_OPEN_DOCUMENT = "android.intent.action.OPEN_DOCUMENT";
    public static final String ACTION_CREATE_DOCUMENT = "android.intent.action.CREATE_DOCUMENT";
    public static final String ACTION_SEND = "android.intent.action.SEND";
    public static final String ACTION_SEND_MULTIPLE = "android.intent.action.SEND_MULTIPLE";
    public static final String ACTION_SENDTO = "android.intent.action.SENDTO";
    public static final String ACTION_DIAL = "android.intent.action.DIAL";
    public static final String ACTION_CALL = "android.intent.action.CALL";
    public static final String ACTION_SEARCH = "android.intent.action.SEARCH";
    public static final String ACTION_WEB_SEARCH = "android.intent.action.WEB_SEARCH";
    public static final String ACTION_CHOOSER = "android.intent.action.CHOOSER";
    public static final String ACTION_ANSWER = "android.intent.action.ANSWER";
    public static final String ACTION_RUN = "android.intent.action.RUN";
    public static final String ACTION_SYNC = "android.intent.action.SYNC";
    public static final String ACTION_SET_WALLPAPER = "android.intent.action.SET_WALLPAPER";
    public static final String ACTION_APPLICATION_PREFERENCES = "android.intent.action.APPLICATION_PREFERENCES";
    public static final String ACTION_BOOT_COMPLETED = "android.intent.action.BOOT_COMPLETED";
    public static final String ACTION_SCREEN_ON = "android.intent.action.SCREEN_ON";
    public static final String ACTION_SCREEN_OFF = "android.intent.action.SCREEN_OFF";
    public static final String ACTION_TIME_TICK = "android.intent.action.TIME_TICK";
    public static final String ACTION_TIMEZONE_CHANGED = "android.intent.action.TIMEZONE_CHANGED";
    public static final String ACTION_LOCALE_CHANGED = "android.intent.action.LOCALE_CHANGED";
    public static final String ACTION_BATTERY_CHANGED = "android.intent.action.BATTERY_CHANGED";
    public static final String ACTION_CONFIGURATION_CHANGED = "android.intent.action.CONFIGURATION_CHANGED";
    public static final String ACTION_PACKAGE_ADDED = "android.intent.action.PACKAGE_ADDED";

    public static final String CATEGORY_DEFAULT = "android.intent.category.DEFAULT";
    public static final String CATEGORY_BROWSABLE = "android.intent.category.BROWSABLE";
    public static final String CATEGORY_LAUNCHER = "android.intent.category.LAUNCHER";
    public static final String CATEGORY_HOME = "android.intent.category.HOME";
    public static final String CATEGORY_INFO = "android.intent.category.INFO";
    public static final String CATEGORY_ALTERNATIVE = "android.intent.category.ALTERNATIVE";
    public static final String CATEGORY_SELECTED_ALTERNATIVE = "android.intent.category.SELECTED_ALTERNATIVE";
    public static final String CATEGORY_OPENABLE = "android.intent.category.OPENABLE";
    public static final String CATEGORY_PREFERENCE = "android.intent.category.PREFERENCE";
    public static final String CATEGORY_LEANBACK_LAUNCHER = "android.intent.category.LEANBACK_LAUNCHER";

    public static final String EXTRA_TEXT = "android.intent.extra.TEXT";
    public static final String EXTRA_HTML_TEXT = "android.intent.extra.HTML_TEXT";
    public static final String EXTRA_SUBJECT = "android.intent.extra.SUBJECT";
    public static final String EXTRA_TITLE = "android.intent.extra.TITLE";
    public static final String EXTRA_EMAIL = "android.intent.extra.EMAIL";
    public static final String EXTRA_CC = "android.intent.extra.CC";
    public static final String EXTRA_BCC = "android.intent.extra.BCC";
    public static final String EXTRA_STREAM = "android.intent.extra.STREAM";
    public static final String EXTRA_INTENT = "android.intent.extra.INTENT";
    public static final String EXTRA_INITIAL_INTENTS = "android.intent.extra.INITIAL_INTENTS";
    public static final String EXTRA_MIME_TYPES = "android.intent.extra.MIME_TYPES";
    public static final String EXTRA_ALLOW_MULTIPLE = "android.intent.extra.ALLOW_MULTIPLE";
    public static final String EXTRA_LOCAL_ONLY = "android.intent.extra.LOCAL_ONLY";
    public static final String EXTRA_PHONE_NUMBER = "android.intent.extra.PHONE_NUMBER";
    public static final String EXTRA_UID = "android.intent.extra.UID";
    public static final String EXTRA_PACKAGE_NAME = "android.intent.extra.PACKAGE_NAME";
    public static final String EXTRA_KEY_EVENT = "android.intent.extra.KEY_EVENT";
    public static final String EXTRA_REFERRER = "android.intent.extra.REFERRER";

    public static final int FLAG_GRANT_READ_URI_PERMISSION = 0x00000001;
    public static final int FLAG_GRANT_WRITE_URI_PERMISSION = 0x00000002;
    public static final int FLAG_FROM_BACKGROUND = 0x00000004;
    public static final int FLAG_DEBUG_LOG_RESOLUTION = 0x00000008;
    public static final int FLAG_EXCLUDE_STOPPED_PACKAGES = 0x00000010;
    public static final int FLAG_INCLUDE_STOPPED_PACKAGES = 0x00000020;
    public static final int FLAG_GRANT_PERSISTABLE_URI_PERMISSION = 0x00000040;
    public static final int FLAG_GRANT_PREFIX_URI_PERMISSION = 0x00000080;
    public static final int FLAG_ACTIVITY_NO_HISTORY = 0x40000000;
    public static final int FLAG_ACTIVITY_SINGLE_TOP = 0x20000000;
    public static final int FLAG_ACTIVITY_NEW_TASK = 0x10000000;
    public static final int FLAG_ACTIVITY_MULTIPLE_TASK = 0x08000000;
    public static final int FLAG_ACTIVITY_CLEAR_TOP = 0x04000000;
    public static final int FLAG_ACTIVITY_FORWARD_RESULT = 0x02000000;
    public static final int FLAG_ACTIVITY_PREVIOUS_IS_TOP = 0x01000000;
    public static final int FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS = 0x00800000;
    public static final int FLAG_ACTIVITY_BROUGHT_TO_FRONT = 0x00400000;
    public static final int FLAG_ACTIVITY_RESET_TASK_IF_NEEDED = 0x00200000;
    public static final int FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY = 0x00100000;
    public static final int FLAG_ACTIVITY_NEW_DOCUMENT = 0x00080000;
    public static final int FLAG_ACTIVITY_NO_USER_ACTION = 0x00040000;
    public static final int FLAG_ACTIVITY_REORDER_TO_FRONT = 0X00020000;
    public static final int FLAG_ACTIVITY_NO_ANIMATION = 0X00010000;
    public static final int FLAG_ACTIVITY_CLEAR_TASK = 0X00008000;
    public static final int FLAG_ACTIVITY_TASK_ON_HOME = 0X00004000;
    public static final int FLAG_ACTIVITY_RETAIN_IN_RECENTS = 0x00002000;
    public static final int FLAG_ACTIVITY_LAUNCH_ADJACENT = 0x00001000;
    public static final int FLAG_ACTIVITY_MATCH_EXTERNAL = 0x00000800;
    public static final int FLAG_RECEIVER_REGISTERED_ONLY = 0x40000000;
    public static final int FLAG_RECEIVER_REPLACE_PENDING = 0x20000000;
    public static final int FLAG_RECEIVER_FOREGROUND = 0x10000000;

    public static final int URI_INTENT_SCHEME = 1 << 0;
    public static final int URI_ANDROID_APP_SCHEME = 1 << 1;
    public static final int URI_ALLOW_UNSAFE = 1 << 2;

    public static final int FILL_IN_ACTION = 1 << 0;
    public static final int FILL_IN_DATA = 1 << 1;
    public static final int FILL_IN_CATEGORIES = 1 << 2;
    public static final int FILL_IN_COMPONENT = 1 << 3;
    public static final int FILL_IN_PACKAGE = 1 << 4;
    public static final int FILL_IN_SOURCE_BOUNDS = 1 << 5;
    public static final int FILL_IN_SELECTOR = 1 << 6;
    public static final int FILL_IN_CLIP_DATA = 1 << 7;

    private String mAction;
    private Uri mData;
    private String mType;
    private String mPackage;
    private ComponentName mComponent;
    private int mFlags;
    private HashSet<String> mCategories;
    private Bundle mExtras;
    private String mIdentifier;

    public Intent() {}

    public Intent(Intent o) {
        mAction = o.mAction;
        mData = o.mData;
        mType = o.mType;
        mPackage = o.mPackage;
        mComponent = o.mComponent;
        mFlags = o.mFlags;
        mIdentifier = o.mIdentifier;
        if (o.mCategories != null) mCategories = new HashSet<String>(o.mCategories);
        if (o.mExtras != null) mExtras = new Bundle(o.mExtras);
    }

    public Intent(String action) {
        mAction = action;
    }

    public Intent(String action, Uri uri) {
        mAction = action;
        mData = uri;
    }

    public Intent(Context packageContext, Class<?> cls) {
        mComponent = new ComponentName(packageContext, cls);
    }

    public Intent(String action, Uri uri, Context packageContext, Class<?> cls) {
        mAction = action;
        mData = uri;
        mComponent = new ComponentName(packageContext, cls);
    }

    public static Intent makeMainActivity(ComponentName mainActivity) {
        Intent intent = new Intent(ACTION_MAIN);
        intent.setComponent(mainActivity);
        intent.addCategory(CATEGORY_LAUNCHER);
        return intent;
    }

    public static Intent makeRestartActivityTask(ComponentName mainActivity) {
        Intent intent = makeMainActivity(mainActivity);
        intent.addFlags(FLAG_ACTIVITY_NEW_TASK | FLAG_ACTIVITY_CLEAR_TASK);
        return intent;
    }

    public static Intent createChooser(Intent target, CharSequence title) {
        Intent intent = new Intent(ACTION_CHOOSER);
        intent.putExtra(EXTRA_INTENT, target);
        if (title != null) intent.putExtra(EXTRA_TITLE, title);
        return intent;
    }

    @Override
    public Object clone() {
        return new Intent(this);
    }

    public Intent cloneFilter() {
        Intent i = new Intent();
        i.mAction = mAction;
        i.mData = mData;
        i.mType = mType;
        i.mPackage = mPackage;
        i.mComponent = mComponent;
        if (mCategories != null) i.mCategories = new HashSet<String>(mCategories);
        return i;
    }

    // --- action, data, type ------------------------------------------------------

    public String getAction() {
        return mAction;
    }

    public Intent setAction(String action) {
        mAction = action != null ? action.intern() : null;
        return this;
    }

    public Uri getData() {
        return mData;
    }

    public String getDataString() {
        return mData != null ? mData.toString() : null;
    }

    public String getScheme() {
        return mData != null ? mData.getScheme() : null;
    }

    public Intent setData(Uri data) {
        mData = data;
        mType = null;
        return this;
    }

    public Intent setDataAndNormalize(Uri data) {
        return setData(data.normalizeScheme());
    }

    public String getType() {
        return mType;
    }

    public Intent setType(String type) {
        mData = null;
        mType = type;
        return this;
    }

    public Intent setTypeAndNormalize(String type) {
        return setType(normalizeMimeType(type));
    }

    public Intent setDataAndType(Uri data, String type) {
        mData = data;
        mType = type;
        return this;
    }

    public Intent setDataAndTypeAndNormalize(Uri data, String type) {
        return setDataAndType(data.normalizeScheme(), normalizeMimeType(type));
    }

    public static String normalizeMimeType(String type) {
        if (type == null) return null;
        type = type.trim().toLowerCase(Locale.ROOT);
        final int semicolonIndex = type.indexOf(';');
        if (semicolonIndex != -1) type = type.substring(0, semicolonIndex);
        return type;
    }

    public String resolveType(Context context) {
        return mType;
    }

    public String resolveTypeIfNeeded(ContentResolver resolver) {
        return mType;
    }

    public String getIdentifier() {
        return mIdentifier;
    }

    public Intent setIdentifier(String identifier) {
        mIdentifier = identifier;
        return this;
    }

    // --- target -----------------------------------------------------------------------

    public String getPackage() {
        return mPackage;
    }

    public Intent setPackage(String packageName) {
        mPackage = packageName;
        return this;
    }

    public ComponentName getComponent() {
        return mComponent;
    }

    public Intent setComponent(ComponentName component) {
        mComponent = component;
        return this;
    }

    public Intent setClassName(String packageName, String className) {
        mComponent = new ComponentName(packageName, className);
        return this;
    }

    public Intent setClassName(Context packageContext, String className) {
        mComponent = new ComponentName(packageContext, className);
        return this;
    }

    public Intent setClass(Context packageContext, Class<?> cls) {
        mComponent = new ComponentName(packageContext, cls);
        return this;
    }

    public ComponentName resolveActivity(PackageManager pm) {
        if (mComponent != null) return mComponent;
        ResolveInfo info = pm.resolveActivity(this, PackageManager.MATCH_DEFAULT_ONLY);
        if (info != null && info.activityInfo != null) {
            return new ComponentName(info.activityInfo.applicationInfo.packageName, info.activityInfo.name);
        }
        return null;
    }

    public ActivityInfo resolveActivityInfo(PackageManager pm, int flags) {
        ResolveInfo info = pm.resolveActivity(this, flags);
        return info != null ? info.activityInfo : null;
    }

    // --- flags and categories -------------------------------------------------------------

    public int getFlags() {
        return mFlags;
    }

    public Intent setFlags(int flags) {
        mFlags = flags;
        return this;
    }

    public Intent addFlags(int flags) {
        mFlags |= flags;
        return this;
    }

    public void removeFlags(int flags) {
        mFlags &= ~flags;
    }

    public Set<String> getCategories() {
        return mCategories;
    }

    public boolean hasCategory(String category) {
        return mCategories != null && mCategories.contains(category);
    }

    public Intent addCategory(String category) {
        if (mCategories == null) mCategories = new HashSet<String>();
        mCategories.add(category.intern());
        return this;
    }

    public void removeCategory(String category) {
        if (mCategories != null) {
            mCategories.remove(category);
            if (mCategories.isEmpty()) mCategories = null;
        }
    }

    // --- extras -----------------------------------------------------------------------

    public Bundle getExtras() {
        return mExtras != null ? new Bundle(mExtras) : null;
    }

    public boolean hasExtra(String name) {
        return mExtras != null && mExtras.containsKey(name);
    }

    public boolean hasFileDescriptors() {
        return false;
    }

    public void setExtrasClassLoader(ClassLoader loader) {
        if (mExtras != null) mExtras.setClassLoader(loader);
    }

    private Bundle extras() {
        if (mExtras == null) mExtras = new Bundle();
        return mExtras;
    }

    public Intent putExtras(Intent src) {
        if (src.mExtras != null) extras().putAll(src.mExtras);
        return this;
    }

    public Intent putExtras(Bundle extras) {
        extras().putAll(extras);
        return this;
    }

    public Intent replaceExtras(Intent src) {
        mExtras = src.mExtras != null ? new Bundle(src.mExtras) : null;
        return this;
    }

    public Intent replaceExtras(Bundle extras) {
        mExtras = extras != null ? new Bundle(extras) : null;
        return this;
    }

    public void removeExtra(String name) {
        if (mExtras != null) {
            mExtras.remove(name);
            if (mExtras.size() == 0) mExtras = null;
        }
    }

    public Intent putExtra(String name, boolean value) {
        extras().putBoolean(name, value);
        return this;
    }

    public Intent putExtra(String name, byte value) {
        extras().putByte(name, value);
        return this;
    }

    public Intent putExtra(String name, char value) {
        extras().putChar(name, value);
        return this;
    }

    public Intent putExtra(String name, short value) {
        extras().putShort(name, value);
        return this;
    }

    public Intent putExtra(String name, int value) {
        extras().putInt(name, value);
        return this;
    }

    public Intent putExtra(String name, long value) {
        extras().putLong(name, value);
        return this;
    }

    public Intent putExtra(String name, float value) {
        extras().putFloat(name, value);
        return this;
    }

    public Intent putExtra(String name, double value) {
        extras().putDouble(name, value);
        return this;
    }

    public Intent putExtra(String name, String value) {
        extras().putString(name, value);
        return this;
    }

    public Intent putExtra(String name, CharSequence value) {
        extras().putCharSequence(name, value);
        return this;
    }

    public Intent putExtra(String name, Parcelable value) {
        extras().putParcelable(name, value);
        return this;
    }

    public Intent putExtra(String name, Parcelable[] value) {
        extras().putParcelableArray(name, value);
        return this;
    }

    public Intent putParcelableArrayListExtra(String name, ArrayList<? extends Parcelable> value) {
        extras().putParcelableArrayList(name, value);
        return this;
    }

    public Intent putIntegerArrayListExtra(String name, ArrayList<Integer> value) {
        extras().putIntegerArrayList(name, value);
        return this;
    }

    public Intent putStringArrayListExtra(String name, ArrayList<String> value) {
        extras().putStringArrayList(name, value);
        return this;
    }

    public Intent putCharSequenceArrayListExtra(String name, ArrayList<CharSequence> value) {
        extras().putCharSequenceArrayList(name, value);
        return this;
    }

    public Intent putExtra(String name, Serializable value) {
        extras().putSerializable(name, value);
        return this;
    }

    public Intent putExtra(String name, boolean[] value) {
        extras().putBooleanArray(name, value);
        return this;
    }

    public Intent putExtra(String name, byte[] value) {
        extras().putByteArray(name, value);
        return this;
    }

    public Intent putExtra(String name, short[] value) {
        extras().putShortArray(name, value);
        return this;
    }

    public Intent putExtra(String name, char[] value) {
        extras().putCharArray(name, value);
        return this;
    }

    public Intent putExtra(String name, int[] value) {
        extras().putIntArray(name, value);
        return this;
    }

    public Intent putExtra(String name, long[] value) {
        extras().putLongArray(name, value);
        return this;
    }

    public Intent putExtra(String name, float[] value) {
        extras().putFloatArray(name, value);
        return this;
    }

    public Intent putExtra(String name, double[] value) {
        extras().putDoubleArray(name, value);
        return this;
    }

    public Intent putExtra(String name, String[] value) {
        extras().putStringArray(name, value);
        return this;
    }

    public Intent putExtra(String name, CharSequence[] value) {
        extras().putCharSequenceArray(name, value);
        return this;
    }

    public Intent putExtra(String name, Bundle value) {
        extras().putBundle(name, value);
        return this;
    }

    public boolean getBooleanExtra(String name, boolean defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getBoolean(name, defaultValue);
    }

    public byte getByteExtra(String name, byte defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getByte(name, defaultValue);
    }

    public short getShortExtra(String name, short defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getShort(name, defaultValue);
    }

    public char getCharExtra(String name, char defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getChar(name, defaultValue);
    }

    public int getIntExtra(String name, int defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getInt(name, defaultValue);
    }

    public long getLongExtra(String name, long defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getLong(name, defaultValue);
    }

    public float getFloatExtra(String name, float defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getFloat(name, defaultValue);
    }

    public double getDoubleExtra(String name, double defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getDouble(name, defaultValue);
    }

    public String getStringExtra(String name) {
        return mExtras == null ? null : mExtras.getString(name);
    }

    public CharSequence getCharSequenceExtra(String name) {
        return mExtras == null ? null : mExtras.getCharSequence(name);
    }

    public <T extends Parcelable> T getParcelableExtra(String name) {
        return mExtras == null ? null : mExtras.<T>getParcelable(name);
    }

    public <T> T getParcelableExtra(String name, Class<T> clazz) {
        return mExtras == null ? null : mExtras.getParcelable(name, clazz);
    }

    public Parcelable[] getParcelableArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getParcelableArray(name);
    }

    public <T extends Parcelable> ArrayList<T> getParcelableArrayListExtra(String name) {
        return mExtras == null ? null : mExtras.<T>getParcelableArrayList(name);
    }

    public Serializable getSerializableExtra(String name) {
        return mExtras == null ? null : mExtras.getSerializable(name);
    }

    public <T extends Serializable> T getSerializableExtra(String name, Class<T> clazz) {
        return mExtras == null ? null : mExtras.getSerializable(name, clazz);
    }

    public ArrayList<Integer> getIntegerArrayListExtra(String name) {
        return mExtras == null ? null : mExtras.getIntegerArrayList(name);
    }

    public ArrayList<String> getStringArrayListExtra(String name) {
        return mExtras == null ? null : mExtras.getStringArrayList(name);
    }

    public ArrayList<CharSequence> getCharSequenceArrayListExtra(String name) {
        return mExtras == null ? null : mExtras.getCharSequenceArrayList(name);
    }

    public boolean[] getBooleanArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getBooleanArray(name);
    }

    public byte[] getByteArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getByteArray(name);
    }

    public short[] getShortArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getShortArray(name);
    }

    public char[] getCharArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getCharArray(name);
    }

    public int[] getIntArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getIntArray(name);
    }

    public long[] getLongArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getLongArray(name);
    }

    public float[] getFloatArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getFloatArray(name);
    }

    public double[] getDoubleArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getDoubleArray(name);
    }

    public String[] getStringArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getStringArray(name);
    }

    public CharSequence[] getCharSequenceArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getCharSequenceArray(name);
    }

    public Bundle getBundleExtra(String name) {
        return mExtras == null ? null : mExtras.getBundle(name);
    }

    // --- comparison ---------------------------------------------------------------------

    public boolean filterEquals(Intent other) {
        if (other == null) return false;
        return Objects.equals(mAction, other.mAction) && Objects.equals(mData, other.mData)
                && Objects.equals(mType, other.mType) && Objects.equals(mIdentifier, other.mIdentifier)
                && Objects.equals(mPackage, other.mPackage) && Objects.equals(mComponent, other.mComponent)
                && Objects.equals(mCategories, other.mCategories);
    }

    public int filterHashCode() {
        int code = 0;
        if (mAction != null) code += mAction.hashCode();
        if (mData != null) code += mData.hashCode();
        if (mType != null) code += mType.hashCode();
        if (mIdentifier != null) code += mIdentifier.hashCode();
        if (mPackage != null) code += mPackage.hashCode();
        if (mComponent != null) code += mComponent.hashCode();
        if (mCategories != null) code += mCategories.hashCode();
        return code;
    }

    public int fillIn(Intent other, int flags) {
        int changes = 0;
        if (other.mAction != null && (mAction == null || (flags & FILL_IN_ACTION) != 0)) {
            mAction = other.mAction;
            changes |= FILL_IN_ACTION;
        }
        if ((other.mData != null || other.mType != null)
                && ((mData == null && mType == null) || (flags & FILL_IN_DATA) != 0)) {
            mData = other.mData;
            mType = other.mType;
            changes |= FILL_IN_DATA;
        }
        if (other.mCategories != null && (mCategories == null || (flags & FILL_IN_CATEGORIES) != 0)) {
            mCategories = new HashSet<String>(other.mCategories);
            changes |= FILL_IN_CATEGORIES;
        }
        if (other.mPackage != null && (mPackage == null || (flags & FILL_IN_PACKAGE) != 0)) {
            mPackage = other.mPackage;
            changes |= FILL_IN_PACKAGE;
        }
        if (other.mComponent != null && (flags & FILL_IN_COMPONENT) != 0) {
            mComponent = other.mComponent;
            changes |= FILL_IN_COMPONENT;
        }
        mFlags |= other.mFlags;
        if (other.mExtras != null) {
            Bundle merged = new Bundle(other.mExtras);
            if (mExtras != null) merged.putAll(mExtras);
            mExtras = merged;
        }
        return changes;
    }

    // --- parceling ---------------------------------------------------------------------

    public static final Parcelable.Creator<Intent> CREATOR = new Parcelable.Creator<Intent>() {
        public Intent createFromParcel(Parcel in) {
            Intent i = new Intent();
            i.readFromParcel(in);
            return i;
        }

        public Intent[] newArray(int size) {
            return new Intent[size];
        }
    };

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
        out.writeString(mAction);
        Uri.writeToParcel(out, mData);
        out.writeString(mType);
        out.writeString(mIdentifier);
        out.writeInt(mFlags);
        out.writeString(mPackage);
        out.writeString(mComponent != null ? mComponent.flattenToString() : null);
        if (mCategories != null) {
            out.writeInt(mCategories.size());
            for (String c : mCategories) out.writeString(c);
        } else {
            out.writeInt(0);
        }
        out.writeBundle(mExtras);
    }

    public void readFromParcel(Parcel in) {
        setAction(in.readString());
        mData = Uri.CREATOR.createFromParcel(in);
        mType = in.readString();
        mIdentifier = in.readString();
        mFlags = in.readInt();
        mPackage = in.readString();
        String component = in.readString();
        mComponent = component != null ? ComponentName.unflattenFromString(component) : null;
        int n = in.readInt();
        mCategories = null;
        for (int i = 0; i < n; i++) addCategory(in.readString());
        mExtras = in.readBundle();
    }

    /** A copy made by flattening, as the system makes when an intent crosses processes. */
    public Intent copyThroughParcel(ClassLoader loader) {
        Parcel p = Parcel.obtain();
        writeToParcel(p, 0);
        p.setDataPosition(0);
        Intent copy = new Intent();
        copy.readFromParcel(p);
        if (copy.mExtras != null) copy.mExtras.setClassLoader(loader);
        p.recycle();
        return copy;
    }

    // --- URI form ------------------------------------------------------------------------

    public String toUri(int flags) {
        StringBuilder uri = new StringBuilder(128);
        if (mData != null) uri.append(mData.toString());
        uri.append("#Intent;");
        if (mAction != null) uri.append("action=").append(Uri.encode(mAction)).append(';');
        if (mCategories != null) {
            for (String c : mCategories) uri.append("category=").append(Uri.encode(c)).append(';');
        }
        if (mType != null) uri.append("type=").append(Uri.encode(mType, "/")).append(';');
        if (mFlags != 0) uri.append("launchFlags=0x").append(Integer.toHexString(mFlags)).append(';');
        if (mPackage != null) uri.append("package=").append(Uri.encode(mPackage)).append(';');
        if (mComponent != null) {
            uri.append("component=").append(Uri.encode(mComponent.flattenToShortString(), "/")).append(';');
        }
        uri.append("end");
        return uri.toString();
    }

    @Deprecated
    public String toURI() {
        return toUri(0);
    }

    public static Intent parseUri(String uri, int flags) throws java.net.URISyntaxException {
        int i = uri.lastIndexOf("#Intent;");
        if (i < 0) return new Intent(ACTION_VIEW, Uri.parse(uri));
        Intent intent = new Intent(ACTION_VIEW);
        String data = i > 0 ? uri.substring(0, i) : null;
        int pos = i + 8;
        while (pos < uri.length() && !uri.startsWith("end", pos)) {
            int eq = uri.indexOf('=', pos);
            int semi = uri.indexOf(';', pos);
            if (eq < 0 || semi < 0 || eq > semi) throw new java.net.URISyntaxException(uri, "bad intent URI", pos);
            String key = uri.substring(pos, eq);
            String value = Uri.decode(uri.substring(eq + 1, semi));
            if (key.equals("action")) intent.setAction(value);
            else if (key.equals("category")) intent.addCategory(value);
            else if (key.equals("type")) intent.mType = value;
            else if (key.equals("launchFlags")) intent.mFlags = Integer.decode(value);
            else if (key.equals("package")) intent.mPackage = value;
            else if (key.equals("component")) intent.mComponent = ComponentName.unflattenFromString(value);
            else if (key.startsWith("S.")) intent.putExtra(key.substring(2), value);
            pos = semi + 1;
        }
        if (data != null) intent.mData = Uri.parse(data);
        return intent;
    }

    public static Intent getIntent(String uri) throws java.net.URISyntaxException {
        return parseUri(uri, 0);
    }

    @Override
    public String toString() {
        StringBuilder b = new StringBuilder("Intent { ");
        if (mAction != null) b.append("act=").append(mAction).append(' ');
        if (mCategories != null) b.append("cat=").append(mCategories).append(' ');
        if (mData != null) b.append("dat=").append(mData.getScheme()).append(":... ");
        if (mType != null) b.append("typ=").append(mType).append(' ');
        if (mFlags != 0) b.append("flg=0x").append(Integer.toHexString(mFlags)).append(' ');
        if (mPackage != null) b.append("pkg=").append(mPackage).append(' ');
        if (mComponent != null) b.append("cmp=").append(mComponent.flattenToShortString()).append(' ');
        if (mExtras != null) b.append("(has extras) ");
        return b.append('}').toString();
    }
}
