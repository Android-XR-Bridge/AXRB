#include <jni.h>
#include <stdarg.h>
static double call_variadic(JNIEnv* env,jclass type,jmethodID method,...) {
    va_list args;va_start(args,method);
    double value=(*env)->CallStaticDoubleMethodV(env,type,method,args);va_end(args);return value;
}
JNIEXPORT jdouble JNICALL arm_variadic(JNIEnv* env,jclass type) {
    jmethodID method=(*env)->GetStaticMethodID(env,type,"callbackMixed","(JDJDJDJDJDJDJDJDJD)D");
    if(!method)return -1;
    return (*env)->CallStaticDoubleMethod(env,type,method,(jlong)1,1.25,(jlong)2,2.25,(jlong)3,3.25,
        (jlong)4,4.25,(jlong)5,5.25,(jlong)6,6.25,(jlong)7,7.25,(jlong)8,8.25,(jlong)9,9.25)
        +call_variadic(env,type,method,(jlong)1,1.25,(jlong)2,2.25,(jlong)3,3.25,
        (jlong)4,4.25,(jlong)5,5.25,(jlong)6,6.25,(jlong)7,7.25,(jlong)8,8.25,(jlong)9,9.25);
}
__attribute__((noinline)) static int deep_stack(int depth) {
    volatile unsigned char bytes[16384];bytes[0]=(unsigned char)depth;bytes[16383]=(unsigned char)(depth+1);
    return (depth?deep_stack(depth-1):0)+bytes[0]+bytes[16383];
}
JNIEXPORT jint JNICALL arm_deep_stack(JNIEnv* env,jclass type) {(void)env;(void)type;return deep_stack(32);}
static JavaVM* cached_vm;
static jlong dynamic_add(JNIEnv* env,jclass type,jlong a,jlong b){(void)env;(void)type;return a+b+100;}
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm,void* reserved) {
    (void)reserved;JNIEnv* env=0;
    if((*vm)->GetEnv(vm,(void**)&env,JNI_VERSION_1_6)!=JNI_OK)return JNI_ERR;
    jclass type=(*env)->FindClass(env,"NativeArm");if(!type)return JNI_ERR;
    JNINativeMethod method={"dynamicAdd","(JJ)J",(void*)dynamic_add};
    if((*env)->RegisterNatives(env,type,&method,1))return JNI_ERR;
    cached_vm=vm;return JNI_VERSION_1_6;
}
/* Deliberately dependency-free ARM ELF. Production libc/loader support is a
 * separate stage; all Java services here go through the actual JNI table. */
JNIEXPORT jlong JNICALL arm_round_trip(JNIEnv* env,jclass type,jlong value,jint depth) {
    if(cached_vm){JNIEnv* other=0;if((*cached_vm)->GetEnv(cached_vm,(void**)&other,JNI_VERSION_1_6)!=JNI_OK || other!=env)return -999;}
    jmethodID method=(*env)->GetStaticMethodID(env,type,"callback","(JI)J");
    if(!method)return -1;
    jvalue args[2];args[0].j=value;args[1].i=depth;
    return (*env)->CallStaticLongMethodA(env,type,method,args)+5;
}
JNIEXPORT jdouble JNICALL arm_mix(JNIEnv* env,jclass type,
    jlong a,jdouble b,jlong c,jdouble d,jlong e,jdouble f,jlong g,jdouble h,
    jlong i,jdouble j,jlong k,jdouble l,jlong m,jdouble n,jlong o,jdouble p,jlong q,jdouble r) {
    (void)env;(void)type;
    return a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q+r;
}
JNIEXPORT jstring JNICALL arm_echo(JNIEnv* env,jclass type,jstring value) {
    (void)type;
    const char* chars=(*env)->GetStringUTFChars(env,value,0);if(!chars)return 0;
    jstring result=(*env)->NewStringUTF(env,chars);
    (*env)->ReleaseStringUTFChars(env,value,chars);return result;
}
JNIEXPORT jint JNICALL arm_array(JNIEnv* env,jclass type,jintArray value) {
    (void)type;jint values[4];
    if((*env)->GetArrayLength(env,value)!=4)return -1;
    (*env)->GetIntArrayRegion(env,value,0,4,values);
    int sum=0;for(int i=0;i<4;i++){values[i]+=i;sum+=values[i];}
    (*env)->SetIntArrayRegion(env,value,0,4,values);return sum;
}
JNIEXPORT void JNICALL arm_throw(JNIEnv* env,jclass type) {
    (void)type;jclass exception=(*env)->FindClass(env,"java/lang/IllegalArgumentException");
    if(exception)(*env)->ThrowNew(env,exception,"ARM exception");
}
JNIEXPORT void JNICALL arm_unsupported(JNIEnv* env,jclass type) {
    (void)type;(*env)->DefineClass(env,"Unsupported",0,0,0);
}
#define EXPORT_JNI(method,implementation) \
    JNIEXPORT __typeof__(implementation) Java_NativeArm_##method __attribute__((alias(#implementation)));
EXPORT_JNI(roundTrip,arm_round_trip)
EXPORT_JNI(mix,arm_mix)
EXPORT_JNI(echo,arm_echo)
EXPORT_JNI(array,arm_array)
EXPORT_JNI(throwFailure,arm_throw)
EXPORT_JNI(unsupported,arm_unsupported)
EXPORT_JNI(variadic,arm_variadic)
EXPORT_JNI(deepStack,arm_deep_stack)
