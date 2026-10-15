package android.widget;

import android.view.View;
import android.view.ViewGroup;

public interface SpinnerAdapter extends Adapter {
    View getDropDownView(int position, View convertView, ViewGroup parent);
}
