#include <jni.h>
#include <stdint.h>
extern void qb_check(const char*, uint64_t, uint64_t, uint64_t, uint64_t);
static unsigned calls;
static jboolean render(JNIEnv* env, jobject player) {
    (void)env; (void)player;
    qb_check("render exit stops further calls", ++calls, 0, 1, 0);
    return JNI_FALSE;
}
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    JNIEnv* env = 0;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass player = (*env)->FindClass(env, "com/unity3d/player/UnityPlayer");
    JNINativeMethod methods[] = {{"nativeRender", "()Z", (void*)render}};
    if ((*env)->RegisterNatives(env, player, methods, 1) != JNI_OK) return JNI_ERR;
    return JNI_VERSION_1_6;
}
