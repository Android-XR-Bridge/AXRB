package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;

/** Item views are plain TextViews showing toString(): layout resources are not inflated. */
public class ArrayAdapter<T> extends BaseAdapter {
    private final Context mContext;
    private final List<T> mObjects;
    private int mResource;
    private int mDropDownResource;

    public ArrayAdapter(Context context, int resource) {
        this(context, resource, new ArrayList<T>());
    }

    public ArrayAdapter(Context context, int resource, T[] objects) {
        this(context, resource, new ArrayList<T>(Arrays.asList(objects)));
    }

    public ArrayAdapter(Context context, int resource, List<T> objects) {
        mContext = context;
        mResource = mDropDownResource = resource;
        mObjects = objects;
    }

    public ArrayAdapter(Context context, int resource, int textViewResourceId, T[] objects) {
        this(context, resource, objects);
    }

    public ArrayAdapter(Context context, int resource, int textViewResourceId, List<T> objects) {
        this(context, resource, objects);
    }

    public Context getContext() {
        return mContext;
    }

    public void add(T object) {
        mObjects.add(object);
        notifyDataSetChanged();
    }

    public void addAll(Collection<? extends T> collection) {
        mObjects.addAll(collection);
        notifyDataSetChanged();
    }

    public void insert(T object, int index) {
        mObjects.add(index, object);
        notifyDataSetChanged();
    }

    public void remove(T object) {
        mObjects.remove(object);
        notifyDataSetChanged();
    }

    public void clear() {
        mObjects.clear();
        notifyDataSetChanged();
    }

    public int getCount() {
        return mObjects.size();
    }

    public T getItem(int position) {
        return mObjects.get(position);
    }

    public int getPosition(T item) {
        return mObjects.indexOf(item);
    }

    public long getItemId(int position) {
        return position;
    }

    public void setDropDownViewResource(int resource) {
        mDropDownResource = resource;
    }

    public View getView(int position, View convertView, ViewGroup parent) {
        TextView text = convertView instanceof TextView ? (TextView) convertView : new TextView(mContext);
        T item = getItem(position);
        text.setText(item instanceof CharSequence ? (CharSequence) item : String.valueOf(item));
        return text;
    }
}
