public final class NativeArm {
    private static native void registerArm();
    private static native long dynamicAdd(long a,long b);
    private static native long roundTrip(long value,int depth);
    private static native double mix(long a,double b,long c,double d,long e,double f,long g,double h,
        long i,double j,long k,double l,long m,double n,long o,double p,long q,double r);
    private static native String echo(String text);
    private static native int array(int[] values);
    private static native void throwFailure();
    private static native void unsupported();
    private static native double variadic();
    private static native int deepStack();
    private static double callbackMixed(long a,double b,long c,double d,long e,double f,long g,double h,
        long i,double j,long k,double l,long m,double n,long o,double p,long q,double r) {
        return a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q+r;
    }
    private static long callback(long value,int depth) {
        if(depth>0)return roundTrip(value,depth-1);
        return value*7;
    }
    private static volatile Throwable failure;
    public static void main(String[] args) throws Exception {
        if(args.length>0 && "standard".equals(args[0])) {
            System.load("/data/local/tmp/x64-probe/arm64/libarm-jni-fixture.so");
            if(dynamicAdd(12,30)!=142)throw new AssertionError("ARM JNI_OnLoad/RegisterNatives");
        } else {
            System.load("/data/local/tmp/x64-probe/libarm-jni-bridge.so");
            registerArm();
        }
        if(roundTrip(1234,0)!=8643 || roundTrip(-1234,3)!=-8618)
            throw new AssertionError("ARM call/reentrant JNI");
        double result=mix(1,1.25,2,2.25,3,3.25,4,4.25,5,5.25,6,6.25,7,7.25,8,8.25,9,9.25);
        if(result!=92.25)throw new AssertionError("ARM mixed registers/stack: "+result);
        if(variadic()!=184.5)throw new AssertionError("ARM variadic/V JNI register and stack arguments");
        if(deepStack()!=1089)throw new AssertionError("ARM deep native stack");
        String text="ARM JNI \u00e4\u20ac \ud83d\ude80";
        if(!text.equals(echo(text)))throw new AssertionError("ARM string");
        int[] values={10,20,30,40};
        if(array(values)!=106 || values[3]!=43)throw new AssertionError("ARM array");
        try {throwFailure();throw new AssertionError("ARM exception missing");}
        catch(IllegalArgumentException expected){if(!"ARM exception".equals(expected.getMessage()))throw expected;}
        try {unsupported();throw new AssertionError("unsupported JNI was hidden");}
        catch(UnsatisfiedLinkError expected){if(!expected.getMessage().contains("not implemented"))throw expected;}
        Thread[] workers=new Thread[4];
        for(int i=0;i<workers.length;i++){
            final int id=i;
            workers[i]=new Thread(new Runnable(){public void run(){
                try {for(int n=0;n<50;n++)if(roundTrip(id*100+n,1)!=(id*100+n)*7+10)throw new AssertionError("threaded ARM JNI");}
                catch(Throwable error){failure=error;}
            }});
            workers[i].start();
        }
        System.gc();for(Thread worker:workers)worker.join();
        if(failure!=null)throw new AssertionError("ARM worker",failure);
        System.out.println("NATIVE_X64_ARM_JNI_PASS ARM ELF, mixed ABI, reentrancy, strings, arrays, exceptions, 4 threads");
    }
}
