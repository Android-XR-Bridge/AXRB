/* What a native application does with the Java side: attach a thread, look up
   a class and a method, move a string across, and register its own natives.
   Every one of these calls goes through the function table by position, so a
   slot in the wrong place shows up here as the wrong answer. */

#include <jni.h>
#include <stdint.h>
#include <string.h>

extern void qb_check(const char* name, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);
/* How this test is handed the virtual machine, which a real application gets
   from its activity. */
extern JavaVM* qb_java_vm(void);

#define CHECK(name, got, want) qb_check(name, (uint64_t)(got), 0, (uint64_t)(want), 0)

static void a_native(JNIEnv* env, jobject self) {
    (void)env;
    (void)self;
}

void qb_guest_main(void) {
    JavaVM* vm = qb_java_vm();
    CHECK("there is a virtual machine", vm != 0, 1);

    JNIEnv* env = 0;
    CHECK("attaching this thread", (*vm)->AttachCurrentThread(vm, &env, 0), JNI_OK);
    CHECK("attaching gave an environment", env != 0, 1);

    JNIEnv* again = 0;
    CHECK("asking for it again", (*vm)->GetEnv(vm, (void**)&again, JNI_VERSION_1_6), JNI_OK);
    CHECK("and it is the same one", again == env, 1);

    CHECK("the version", (*env)->GetVersion(env), JNI_VERSION_1_6);

    /* Classes and methods come back as handles, and the same name must give
       the same handle twice. */
    jclass activity = (*env)->FindClass(env, "android/app/Activity");
    CHECK("finding a class", activity != 0, 1);
    jclass same = (*env)->FindClass(env, "android/app/Activity");
    CHECK("the same class twice", same == activity, 1);
    jclass other = (*env)->FindClass(env, "android/os/Build");
    CHECK("a different class is different", other != activity, 1);

    jmethodID method = (*env)->GetMethodID(env, activity, "getPackageName", "()Ljava/lang/String;");
    CHECK("finding a method", method != 0, 1);
    jmethodID method_again = (*env)->GetMethodID(env, activity, "getPackageName", "()Ljava/lang/String;");
    CHECK("the same method twice", method_again == method, 1);
    jmethodID different = (*env)->GetMethodID(env, activity, "getPackageName", "()I");
    CHECK("a different signature is a different method", different != method, 1);

    /* Strings have to survive the trip out and back. */
    jstring text = (*env)->NewStringUTF(env, "com.example.game");
    CHECK("making a string", text != 0, 1);
    CHECK("its length", (*env)->GetStringUTFLength(env, text), (int)strlen("com.example.game"));
    const char* read = (*env)->GetStringUTFChars(env, text, 0);
    CHECK("reading it back", read != 0 && strcmp(read, "com.example.game") == 0, 1);
    (*env)->ReleaseStringUTFChars(env, text, read);

    /* References, which an application holds on to across frames. */
    jobject global = (*env)->NewGlobalRef(env, activity);
    CHECK("a global reference", global != 0, 1);
    CHECK("it points at the same thing", (*env)->IsSameObject(env, global, activity), JNI_TRUE);
    (*env)->DeleteGlobalRef(env, global);

    CHECK("nothing has thrown", (*env)->ExceptionCheck(env), JNI_FALSE);

    /* Handing our own functions to the Java side. */
    JNINativeMethod natives[] = {{"aNative", "()V", (void*)a_native}};
    CHECK("registering natives", (*env)->RegisterNatives(env, activity, natives, 1), JNI_OK);

    /* And getting back to the machine from the environment. */
    JavaVM* from_env = 0;
    CHECK("the environment knows its machine", (*env)->GetJavaVM(env, &from_env), JNI_OK);
    CHECK("and it is the same machine", from_env == vm, 1);

    CHECK("detaching", (*vm)->DetachCurrentThread(vm), JNI_OK);
}
