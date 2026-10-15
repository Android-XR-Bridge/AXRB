package android.os;

/**
 * Types that can be written to a {@link Parcel}. The runtime flattens intent
 * extras through Parcel when one activity starts another, as Android does
 * across processes, so the receiving side gets a copy made by the object's
 * own writeToParcel and CREATOR.
 */
public interface Parcelable {
    int PARCELABLE_WRITE_RETURN_VALUE = 0x0001;
    int CONTENTS_FILE_DESCRIPTOR = 0x0001;

    int describeContents();

    void writeToParcel(Parcel dest, int flags);

    public interface Creator<T> {
        T createFromParcel(Parcel source);

        T[] newArray(int size);
    }

    public interface ClassLoaderCreator<T> extends Creator<T> {
        T createFromParcel(Parcel source, ClassLoader loader);
    }
}
