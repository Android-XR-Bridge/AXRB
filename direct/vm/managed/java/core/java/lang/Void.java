package java.lang;

public final class Void {
    @SuppressWarnings("unchecked")
    public static final Class<Void> TYPE = (Class<Void>) Class.getPrimitiveClass("void");

    private Void() {}
}
