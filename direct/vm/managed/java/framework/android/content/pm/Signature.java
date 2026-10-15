package android.content.pm;

/**
 * A package signing certificate. The runner does not verify APK signatures
 * (spec 13.2) and therefore never reports signatures; this type exists for
 * API compatibility only and is never populated.
 */
public class Signature {
    private final byte[] mSignature;

    public Signature(byte[] signature) {
        mSignature = signature.clone();
    }

    public Signature(String text) {
        int n = text.length() / 2;
        mSignature = new byte[n];
        for (int i = 0; i < n; i++) mSignature[i] = (byte) Integer.parseInt(text.substring(i * 2, i * 2 + 2), 16);
    }

    public byte[] toByteArray() {
        return mSignature.clone();
    }

    public String toCharsString() {
        StringBuilder b = new StringBuilder(mSignature.length * 2);
        for (byte v : mSignature) {
            b.append(Character.forDigit((v >> 4) & 0xf, 16)).append(Character.forDigit(v & 0xf, 16));
        }
        return b.toString();
    }

    @Override
    public boolean equals(Object obj) {
        return obj instanceof Signature && java.util.Arrays.equals(mSignature, ((Signature) obj).mSignature);
    }

    @Override
    public int hashCode() {
        return java.util.Arrays.hashCode(mSignature);
    }
}
