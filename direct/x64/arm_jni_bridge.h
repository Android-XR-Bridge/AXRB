#pragma once
#include <jni.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Registers ordinary JNI methods through the ARM bridge. Each fnPtr is the
 * ARM ELF symbol name, not a native function pointer, on input only. */
int axrb_arm_register(JNIEnv*,jclass,const char*,const JNINativeMethod*,int);
long axrb_arm_load(const char*);
void* axrb_arm_trampoline(long,const char*,const char*,unsigned);
void* axrb_arm_pointer(const void*,const char*,unsigned);
int axrb_arm_is_function(const void*);
#ifdef __cplusplus
}
#endif
