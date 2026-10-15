package android.text;

public interface InputFilter {
    CharSequence filter(CharSequence source, int start, int end, Spanned dest, int dstart, int dend);

    class LengthFilter implements InputFilter {
        private final int mMax;

        public LengthFilter(int max) {
            mMax = max;
        }

        public int getMax() {
            return mMax;
        }

        public CharSequence filter(CharSequence source, int start, int end, Spanned dest, int dstart, int dend) {
            int keep = dest == null ? mMax - (end - start) : mMax - (dest.length() - (dend - dstart));
            if (keep <= 0) return "";
            if (keep >= end - start) return null;
            return source.subSequence(start, start + keep);
        }
    }

    class AllCaps implements InputFilter {
        public CharSequence filter(CharSequence source, int start, int end, Spanned dest, int dstart, int dend) {
            return source.subSequence(start, end).toString().toUpperCase();
        }
    }
}
