#include <jni.h>
JNIEXPORT jlong JNICALL Java_NativeArt_nativeRoundTrip(JNIEnv* env,jclass type,jlong value) {
    jmethodID method=(*env)->GetStaticMethodID(env,type,"callback","(J)J");
    if(!method) return -1;
    return (*env)->CallStaticLongMethod(env,type,method,value)+5;
}
