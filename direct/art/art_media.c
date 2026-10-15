#include "../guest/axrb_guest.h"
#include <jni.h>
#include <stdint.h>

extern long axrb_media(int op, long id, long a, long b, long c);
AXRB_HOST_FUNCTION(axrb_media, "art.media");
extern long axrb_media_frame(long surface, long address, int width, int height,
                             long previous);
AXRB_HOST_FUNCTION(axrb_media_frame, "art.media_frame");
static jclass backend;
static jmethodID control, setup, surface, invoke;
static jlong call(JNIEnv *e, jobject p, int op, jlong a, jlong b, jlong c) {
  return (*e)->CallStaticLongMethod(e, backend, control, p, op, a, b, c);
}
static void player_setup(JNIEnv *e, jobject p, jobject weak,
                         jobject attribution, jint session) {
  (void)attribution;
  (*e)->CallStaticVoidMethod(e, backend, setup, p, weak, session);
}
static void player_source(JNIEnv *e, jobject p, jobject fd, jlong offset,
                          jlong length) {
  jclass cls = (*e)->GetObjectClass(e, fd);
  jfieldID descriptor = (*e)->GetFieldID(e, cls, "descriptor", "I");
  if (descriptor)
    call(e, p, 1, (*e)->GetIntField(e, fd, descriptor), offset, length);
}
static void player_surface(JNIEnv *e, jobject p, jobject value) {
  (*e)->CallStaticVoidMethod(e, backend, surface, p, value);
}
static jint player_prepare(JNIEnv *e, jobject p, jobject parcel) {
  (void)parcel;
  return (jint)call(e, p, 3, 0, 0, 0);
}
static jint player_prepare_async(JNIEnv *e, jobject p, jobject parcel) {
  (void)parcel;
  return (jint)call(e, p, 103, 0, 0, 0);
}
#define VOID_CONTROL(name, op)                                                 \
  static void name(JNIEnv *e, jobject p) { call(e, p, op, 0, 0, 0); }
#define INT_CONTROL(name, op)                                                  \
  static jint name(JNIEnv *e, jobject p) {                                     \
    return (jint)call(e, p, op, 0, 0, 0);                                      \
  }
VOID_CONTROL(player_start, 4)
VOID_CONTROL(player_pause, 5)
VOID_CONTROL(player_stop, 6)
VOID_CONTROL(player_release, 12)
VOID_CONTROL(player_reset, 104)
INT_CONTROL(player_position, 10)
INT_CONTROL(player_duration, 11)
INT_CONTROL(player_width, 13)
INT_CONTROL(player_height, 14)
INT_CONTROL(player_session, 100)
static jboolean player_playing(JNIEnv *e, jobject p) {
  return call(e, p, 9, 0, 0, 0) != 0;
}
static jboolean player_looping(JNIEnv *e, jobject p) {
  return call(e, p, 15, 0, 0, 0) != 0;
}
static void player_loop(JNIEnv *e, jobject p, jboolean v) {
  call(e, p, 8, v, 0, 0);
}
static void player_seek(JNIEnv *e, jobject p, jlong ms, jint mode) {
  (void)mode;
  call(e, p, 7, ms, 0, 0);
}
static void player_set_session(JNIEnv *e, jobject p, jint v) {
  call(e, p, 101, v, 0, 0);
}
static void player_noop(JNIEnv *e, jobject p, ...) {
  (void)e;
  (void)p;
}
static jint player_stream(JNIEnv *e, jobject p) {
  (void)e;
  (void)p;
  return 3;
}
static jboolean player_parameter(JNIEnv *e, jobject p, jint key,
                                 jobject parcel) {
  (void)e;
  (void)p;
  (void)key;
  (void)parcel;
  return JNI_TRUE;
}
static jint player_invoke(JNIEnv *e, jobject p, jobject request,
                          jobject reply) {
  return (*e)->CallStaticIntMethod(e, backend, invoke, p, request, reply);
}
static void player_volume(JNIEnv *e, jobject p, jfloat left, jfloat right) {
  (void)e;
  (void)p;
  (void)left;
  (void)right;
}
static jlong player_unsupported(JNIEnv *e, jobject p, ...) {
  (void)p;
  jclass error = (*e)->FindClass(e, "java/lang/UnsupportedOperationException");
  if (error)
    (*e)->ThrowNew(e, error, "This MediaPlayer operation is not supported by the host media backend");
  return 0;
}
static void player_finalize(JNIEnv *e, jobject p) {
  jclass cls = (*e)->GetObjectClass(e, p);
  jfieldID context = (*e)->GetFieldID(e, cls, "mNativeContext", "J");
  if (context && (*e)->GetLongField(e, p, context))
    call(e, p, 12, 0, 0, 0);
}
JNIEXPORT jlong JNICALL Java_com_axrb_runtime_HostMedia_command(
    JNIEnv *e, jclass cls, jint op, jlong id, jlong a, jlong b, jlong c) {
  (void)e;
  (void)cls;
  return axrb_media(op, id, a, b, c);
}
JNIEXPORT jlong JNICALL Java_com_axrb_runtime_HostMedia_readFrame(
    JNIEnv *e, jclass cls, jlong id, jlong dst, jint w, jint h,
    jlong previous) {
  (void)e;
  (void)cls;
  return axrb_media_frame(id, dst, w, h, previous);
}
JNIEXPORT void JNICALL Java_com_axrb_runtime_HostMedia_releaseSurface(
    JNIEnv *e, jclass cls, jlong id) {
  (void)e;
  (void)cls;
  axrb_media(16, id, 0, 0, 0);
}
int register_art_media(JNIEnv *e) {
  backend =
      (*e)->NewGlobalRef(e, (*e)->FindClass(e, "com/axrb/runtime/HostMedia"));
  if (!backend)
    return -1;
  control = (*e)->GetStaticMethodID(e, backend, "control",
                                    "(Landroid/media/MediaPlayer;IJJJ)J");
  setup = (*e)->GetStaticMethodID(
      e, backend, "setup", "(Landroid/media/MediaPlayer;Ljava/lang/Object;I)V");
  surface = (*e)->GetStaticMethodID(
      e, backend, "surface",
      "(Landroid/media/MediaPlayer;Landroid/view/Surface;)V");
  invoke = (*e)->GetStaticMethodID(
      e, backend, "invoke",
      "(Landroid/media/MediaPlayer;Landroid/os/Parcel;Landroid/os/Parcel;)I");
  if (!control || !setup || !surface || !invoke)
    return -1;
  jclass cls = (*e)->FindClass(e, "android/media/MediaPlayer");
  if (!cls)
    return -1;
  // Initialize libmedia_jni before replacing its daemon-dependent methods.
  // Otherwise MediaPlayer's static initializer re-registers the originals.
  if (!(*e)->GetStaticMethodID(e, cls, "native_init", "()V"))
    return -1;
  const JNINativeMethod methods[] = {
      {"native_setup", "(Ljava/lang/Object;Landroid/os/Parcel;I)V",
       (void *)player_setup},
      {"_setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
       (void *)player_source},
      {"_setVideoSurface", "(Landroid/view/Surface;)V", (void *)player_surface},
      {"_prepare", "(Landroid/os/Parcel;)I", (void *)player_prepare},
      {"_prepareAsync", "(Landroid/os/Parcel;)I", (void *)player_prepare_async},
      {"_start", "()V", (void *)player_start},
      {"_pause", "()V", (void *)player_pause},
      {"_stop", "()V", (void *)player_stop},
      {"_release", "()V", (void *)player_release},
      {"_reset", "()V", (void *)player_reset},
      {"native_finalize", "()V", (void *)player_finalize},
      {"isPlaying", "()Z", (void *)player_playing},
      {"isLooping", "()Z", (void *)player_looping},
      {"getCurrentPosition", "()I", (void *)player_position},
      {"getDuration", "()I", (void *)player_duration},
      {"getVideoWidth", "()I", (void *)player_width},
      {"getVideoHeight", "()I", (void *)player_height},
      {"getAudioSessionId", "()I", (void *)player_session},
      {"native_setAudioSessionId", "(I)V", (void *)player_set_session},
      {"setLooping", "(Z)V", (void *)player_loop},
      {"_seekTo", "(JI)V", (void *)player_seek},
      {"_setVolume", "(FF)V", (void *)player_volume},
      {"_setAudioStreamType", "(I)V", (void *)player_noop},
      {"_getAudioStreamType", "()I", (void *)player_stream},
      {"setParameter", "(ILandroid/os/Parcel;)Z", (void *)player_parameter},
      {"native_invoke", "(Landroid/os/Parcel;Landroid/os/Parcel;)I",
       (void *)player_invoke},
      {"native_enableDeviceCallback", "(Z)V", (void *)player_noop},
      // mNativeContext is a host handle, not an android::MediaPlayer pointer.
      // Override every remaining instance native boundary in the pinned AOSP
      // framework so an unsupported operation raises Java rather than letting
      // libmedia_jni dereference the host handle as an Android object.
#define UNSUPPORTED(name, signature) {name, signature, (void *)player_unsupported}
      UNSUPPORTED("nativeSetDataSource", "(Landroid/os/IBinder;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V"),
      UNSUPPORTED("_setDataSource", "(Landroid/media/MediaDataSource;)V"),
      UNSUPPORTED("native_getMetrics", "()Landroid/os/PersistableBundle;"),
      UNSUPPORTED("setPlaybackParams", "(Landroid/media/PlaybackParams;)V"),
      UNSUPPORTED("getPlaybackParams", "()Landroid/media/PlaybackParams;"),
      UNSUPPORTED("setSyncParams", "(Landroid/media/SyncParams;)V"),
      UNSUPPORTED("getSyncParams", "()Landroid/media/SyncParams;"),
      UNSUPPORTED("_notifyAt", "(J)V"),
      UNSUPPORTED("native_setMetadataFilter", "(Landroid/os/Parcel;)I"),
      UNSUPPORTED("native_getMetadata", "(ZZLandroid/os/Parcel;)Z"),
      UNSUPPORTED("_setAuxEffectSendLevel", "(F)V"),
      UNSUPPORTED("attachAuxEffect", "(I)V"),
      UNSUPPORTED("native_setRetransmitEndpoint", "(Ljava/lang/String;I)I"),
      UNSUPPORTED("setNextMediaPlayer", "(Landroid/media/MediaPlayer;)V"),
      UNSUPPORTED("native_applyVolumeShaper", "(Landroid/media/VolumeShaper$Configuration;Landroid/media/VolumeShaper$Operation;)I"),
      UNSUPPORTED("native_getVolumeShaperState", "(I)Landroid/media/VolumeShaper$State;"),
      UNSUPPORTED("_prepareDrm", "([B[B)V"),
      UNSUPPORTED("_releaseDrm", "()V"),
      UNSUPPORTED("native_setOutputDevice", "(I)Z"),
      UNSUPPORTED("native_getRoutedDeviceIds", "()[I"),
#undef UNSUPPORTED
  };
  return (*e)->RegisterNatives(e, cls, methods,
                               sizeof methods / sizeof methods[0]);
}
