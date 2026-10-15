public class NativeLink {
    private static native long result(long value);
    public static void main(String[] args) {
        try {
            System.load("/data/local/tmp/x64-probe/arm64/libarm-link-bad-init.so");
            throw new AssertionError("Unsupported constructor syscall was silently ignored");
        } catch(UnsatisfiedLinkError expected) {}
        System.load("/data/local/tmp/x64-probe/arm64/libarm-link-parent.so");
        if(result(4)!=132 || result(-4)!=124)throw new AssertionError("ARM dependency/constructor/relocation result");
        System.out.println("NATIVE_X64_ARM_LINK_PASS versioned dependencies, constructors, function/data/weak relocations");
    }
}
