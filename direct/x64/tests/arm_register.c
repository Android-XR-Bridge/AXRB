#include "../arm_jni_bridge.h"
JNIEXPORT void JNICALL Java_NativeArm_registerArm(JNIEnv* env,jclass type) {
    const JNINativeMethod methods[]={
        {"roundTrip","(JI)J",(void*)"arm_round_trip"},
        {"mix","(JDJDJDJDJDJDJDJDJD)D",(void*)"arm_mix"},
        {"echo","(Ljava/lang/String;)Ljava/lang/String;",(void*)"arm_echo"},
        {"array","([I)I",(void*)"arm_array"},
        {"throwFailure","()V",(void*)"arm_throw"},
        {"unsupported","()V",(void*)"arm_unsupported"},
        {"variadic","()D",(void*)"arm_variadic"},
        {"deepStack","()I",(void*)"arm_deep_stack"},
    };
    axrb_arm_register(env,type,"/data/local/tmp/x64-probe/libarm-jni-fixture.so",methods,sizeof methods/sizeof methods[0]);
}
