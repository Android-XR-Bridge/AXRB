#include <jni.h>
extern long dep_value;
extern long dep_answer(long);
extern long optional_value __attribute__((weak));
long* absolute_pointer=&dep_value;
static long initialized;
__attribute__((constructor)) static void initialize(void){initialized=dep_answer(5);}
JNIEXPORT jlong JNICALL Java_NativeLink_result(JNIEnv* env,jclass type,jlong input) {
    (void)env;(void)type;
    return initialized+dep_answer(input)+*absolute_pointer+(&optional_value?10000:0);
}
