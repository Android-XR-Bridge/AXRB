/* x86 Android side: ART owns JNI handles, references, exceptions and GC.
 * ARM execution yields for JNI calls; callbacks then execute normally in ART.
 * No host Windows function is called using the SysV calling convention. */
#include "arm_jni_bridge.h"
#include "arm_bridge_abi.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <android/native_window_jni.h>
#include <sys/mman.h>
#define ARM_CALL_STACK_SIZE (8u*1024u*1024u)
static _Thread_local void* call_stacks[16];
static _Thread_local unsigned call_depth;
static void* acquire_call_stack(void) {
    if(call_depth==16)return 0;
    if(!call_stacks[call_depth]) {
        void* base=mmap(0,ARM_CALL_STACK_SIZE+8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(base==MAP_FAILED)return 0;
        if(mprotect(base,4096,PROT_NONE) || mprotect((char*)base+4096+ARM_CALL_STACK_SIZE,4096,PROT_NONE)) {
            munmap(base,ARM_CALL_STACK_SIZE+8192);return 0;
        }
        call_stacks[call_depth]=(char*)base+4096;
    }
    return call_stacks[call_depth++];
}
#define MAX_METHODS 64
struct Method {uint64_t pc;char shorty[64];int vm_entry;};
static struct Method methods[MAX_METHODS];
static _Atomic unsigned method_count;
static pthread_mutex_t registration=PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t tables_once=PTHREAD_ONCE_INIT;
static uint64_t jni_table[256],vm_table[8],fake_vm;
static _Thread_local uint64_t fake_env;
extern void* axrb_arm_trampolines[MAX_METHODS];
struct Frame {uint64_t gp[6],fp[8][2],stack,result,fp_result;};
_Static_assert(offsetof(struct Frame,stack)==176 && offsetof(struct Frame,result)==184,"trampoline layout");
static long transport(long op,long a,long b) {return syscall(AXRB_ARM_SYSCALL,op,a,b,(long)AXRB_ARM_ABI);}
static void init_tables(void) {
    long gates=transport(AXRB_ARM_GATES,0,0);if(gates<0)return;
    for(unsigned i=0;i<256;i++)jni_table[i]=(uint64_t)gates+8+i*8;
    for(unsigned i=0;i<8;i++)vm_table[i]=(uint64_t)gates+2056+i*8;
    fake_vm=(uint64_t)vm_table;
}
static void fail(JNIEnv* env,const char* message) {
    if((*env)->ExceptionCheck(env)) return;
    jclass type=(*env)->FindClass(env,"java/lang/UnsatisfiedLinkError");
    if(type){(*env)->ThrowNew(env,type,message);(*env)->DeleteLocalRef(env,type);}
}
static int shorty(const char* signature,char out[64]) {
    if(!signature || *signature++!='(') return 0;
    unsigned count=1;
    while(*signature && *signature!=')') {
        if(count>=63) return 0;
        char value=*signature++;
        if(value=='[') {while(*signature=='[')++signature;value=*signature++;out[count++]='L';}
        else out[count++]=value=='L'?'L':value;
        if(value=='L') {const char* end=strchr(signature,';');if(!end) return 0;signature=end+1;}
        else if(!strchr("ZBCSIJFD",value)) return 0;
    }
    if(*signature++!=')' || !*signature) return 0;
    out[0]=strchr("L[",*signature)?'L':*signature;
    if(!strchr("VZBCSIJFDL",out[0])) return 0;
    out[count]=0;return 1;
}
static pthread_mutex_t loading=PTHREAD_MUTEX_INITIALIZER;
long axrb_arm_load(const char* path) {
    // No JNI is available in ELF constructors. A callback/system call that has
    // not been bridged must fail the load rather than silently skip initialization.
    pthread_mutex_lock(&loading);
    long library=transport(AXRB_ARM_LOAD_PATH,(long)path,0);
    if(library<0){pthread_mutex_unlock(&loading);return -1;}
    long pc;
    while((pc=transport(AXRB_ARM_INITIALIZER,library,0))>0) {
        void* stack=malloc(128*1024);int complete=0;
        if(!stack)break;
        struct AxrbArmRegisters r={0};r.pc=(uint64_t)pc;
        r.sp=((uint64_t)stack+128*1024-256)&~(uint64_t)15;
        r.x[30]=(uint64_t)transport(AXRB_ARM_GATES,0,0);
        long invocation=transport(AXRB_ARM_BEGIN,pc,0);
        if(invocation>0) {
            for(unsigned budget=0;budget<100000;budget++) {
                long result=transport(AXRB_ARM_STEP,invocation,(long)&r);
                if(result==1){complete=1;break;}
                if(result!=0)break;
            }
            transport(AXRB_ARM_END,invocation,0);
        }
        free(stack);if(!complete)break;
    }
    if(pc!=0){transport(AXRB_ARM_INITIALIZER,library,1);fprintf(stderr,"ARM bridge: constructor execution failed for %s\n",path);library=-1;}
    pthread_mutex_unlock(&loading);return library;
}
static void* pointer_trampoline(uint64_t pc,const char* descriptor,unsigned length,int vm_entry) {
    if(!descriptor || !length || length>=64 || !strchr("VZBCSIJFDL",descriptor[0]))return 0;
    for(unsigned i=1;i<length;i++)if(!strchr("ZBCSIJFDL",descriptor[i]) || !descriptor[i])return 0;
    if(descriptor[length])return 0;
    pthread_mutex_lock(&registration);
    for(unsigned i=0;i<method_count;i++)if(methods[i].pc==pc && methods[i].vm_entry==vm_entry && !strcmp(methods[i].shorty,descriptor)) {
        pthread_mutex_unlock(&registration);return axrb_arm_trampolines[i];
    }
    if(method_count==MAX_METHODS){pthread_mutex_unlock(&registration);return 0;}
    unsigned slot=method_count;methods[slot].pc=pc;methods[slot].vm_entry=vm_entry;memcpy(methods[slot].shorty,descriptor,length+1);method_count=slot+1;
    pthread_mutex_unlock(&registration);return axrb_arm_trampolines[slot];
}
void* axrb_arm_trampoline(long library,const char* name,const char* descriptor,unsigned length) {
    long pc=transport(AXRB_ARM_SYMBOL,library,(long)name);if(pc<0)return 0;
    int onload=!strcmp(name,"JNI_OnLoad");
    if(onload){descriptor="I";length=1;}
    return pointer_trampoline((uint64_t)pc,descriptor,length,onload);
}
int axrb_arm_is_function(const void* pointer) {return transport(AXRB_ARM_IS_FUNCTION,(long)pointer,0)==1;}
void* axrb_arm_pointer(const void* pointer,const char* descriptor,unsigned length) {
    for(unsigned i=0;i<method_count;i++)if(axrb_arm_trampolines[i]==pointer)return (void*)pointer;
    if(!axrb_arm_is_function(pointer))return 0;
    return pointer_trampoline((uint64_t)pointer,descriptor,length,0);
}
int axrb_arm_register(JNIEnv* env,jclass type,const char* path,const JNINativeMethod* entries,int count) {
    if(count<=0 || count>MAX_METHODS) {fail(env,"invalid ARM native method count");return -1;}
    long library=axrb_arm_load(path);
    if(library<0){fail(env,"ARM ELF loading failed; see runtime diagnostic");return -1;}
    pthread_mutex_lock(&registration);
    if(method_count+(unsigned)count>MAX_METHODS){pthread_mutex_unlock(&registration);fail(env,"ARM trampoline pool exhausted");return -1;}
    JNINativeMethod native[MAX_METHODS];unsigned first=method_count;
    for(int i=0;i<count;i++) {
        long pc=transport(AXRB_ARM_SYMBOL,library,(long)entries[i].fnPtr);
        if(pc<0 || !shorty(entries[i].signature,methods[first+i].shorty)) {
            pthread_mutex_unlock(&registration);fail(env,"ARM symbol/signature unsupported");return -1;
        }
        methods[first+i].pc=(uint64_t)pc;native[i]=entries[i];native[i].fnPtr=axrb_arm_trampolines[first+i];
    }
    method_count=first+(unsigned)count;
    int result=(*env)->RegisterNatives(env,type,native,count);
    pthread_mutex_unlock(&registration);return result;
}
#define SLOT(name) (offsetof(struct JNINativeInterface,name)/sizeof(void*))
struct MethodSignature {jmethodID id;char descriptor[64];struct MethodSignature* next;};
static struct MethodSignature* signatures;
static pthread_mutex_t signature_lock=PTHREAD_MUTEX_INITIALIZER;
static void remember_signature(jmethodID id,const char* signature) {
    if(!id)return;
    pthread_mutex_lock(&signature_lock);
    for(struct MethodSignature* it=signatures;it;it=it->next)if(it->id==id){pthread_mutex_unlock(&signature_lock);return;}
    struct MethodSignature* entry=calloc(1,sizeof(*entry));
    if(entry && shorty(signature,entry->descriptor)){entry->id=id;entry->next=signatures;signatures=entry;}else free(entry);
    pthread_mutex_unlock(&signature_lock);
}
static char class_shorty(JNIEnv* env,jobject type) {
    jclass cls=(*env)->FindClass(env,"java/lang/Class");jmethodID name=(*env)->GetMethodID(env,cls,"getName","()Ljava/lang/String;");
    jstring text=(*env)->CallObjectMethod(env,type,name);const char* utf=(*env)->GetStringUTFChars(env,text,0);char result='L';
    const char* names[]={"void","boolean","byte","char","short","int","long","float","double"};
    const char codes[]="VZBCSIJFD";for(unsigned i=0;i<9;i++)if(!strcmp(utf,names[i]))result=codes[i];
    (*env)->ReleaseStringUTFChars(env,text,utf);(*env)->DeleteLocalRef(env,text);(*env)->DeleteLocalRef(env,cls);return result;
}
static void remember_reflected(JNIEnv* env,jmethodID id,jobject method) {
    jclass cls=(*env)->GetObjectClass(env,method);
    jmethodID params=(*env)->GetMethodID(env,cls,"getParameterTypes","()[Ljava/lang/Class;");
    jobjectArray types=(jobjectArray)(*env)->CallObjectMethod(env,method,params);jsize count=(*env)->GetArrayLength(env,types);
    if(count>62){fail(env,"Reflected JNI method has too many arguments");return;}
    char descriptor[64]={0};descriptor[0]='V';
    jclass method_class=(*env)->FindClass(env,"java/lang/reflect/Method");
    if((*env)->IsInstanceOf(env,method,method_class)) {
        jmethodID ret=(*env)->GetMethodID(env,method_class,"getReturnType","()Ljava/lang/Class;");
        jobject type=(*env)->CallObjectMethod(env,method,ret);descriptor[0]=class_shorty(env,type);(*env)->DeleteLocalRef(env,type);
    }
    for(jsize i=0;i<count;i++){jobject type=(*env)->GetObjectArrayElement(env,types,i);descriptor[i+1]=class_shorty(env,type);(*env)->DeleteLocalRef(env,type);}
    pthread_mutex_lock(&signature_lock);struct MethodSignature* entry=calloc(1,sizeof(*entry));
    if(entry){entry->id=id;memcpy(entry->descriptor,descriptor,64);entry->next=signatures;signatures=entry;}
    pthread_mutex_unlock(&signature_lock);
    (*env)->DeleteLocalRef(env,types);(*env)->DeleteLocalRef(env,method_class);(*env)->DeleteLocalRef(env,cls);
}
struct ArmVaList {uint64_t stack,gr_top,vr_top;int32_t gr_offs,vr_offs;};
static int call_arguments(JNIEnv* env,struct AxrbArmRegisters* r,unsigned method_index,unsigned form,jvalue out[64]) {
    if(form==2)return 1;
    char descriptor[64]={0};
    pthread_mutex_lock(&signature_lock);
    for(struct MethodSignature* it=signatures;it;it=it->next)if(it->id==(jmethodID)r->x[method_index]){memcpy(descriptor,it->descriptor,64);break;}
    pthread_mutex_unlock(&signature_lock);
    if(!descriptor[0]){fail(env,"ARM variadic JNI call has no recorded method signature");return 0;}
    struct ArmVaList va={0};if(form==1)memcpy(&va,(void*)r->x[method_index+1],sizeof va);
    unsigned gp=method_index+1,fp=0;uint64_t stack=r->sp;
    memset(out,0,sizeof(jvalue)*64);
    for(unsigned i=1;descriptor[i];i++) {
        char t=descriptor[i];int floating=t=='D'||t=='F';uint64_t bits=0;
        if(form==1) {
            int32_t* offset=floating?&va.vr_offs:&va.gr_offs;
            if(*offset<0){memcpy(&bits,(void*)((floating?va.vr_top:va.gr_top)+*offset),8);*offset+=floating?16:8;}
            else {memcpy(&bits,(void*)va.stack,8);va.stack+=8;}
        } else if(floating && fp<8)bits=r->v[fp++][0];
        else if(!floating && gp<8)bits=r->x[gp++];
        else {memcpy(&bits,(void*)stack,8);stack+=8;}
        if(t=='F'){double promoted;memcpy(&promoted,&bits,8);out[i-1].f=(float)promoted;}
        else memcpy(&out[i-1],&bits,8);
    }
    return 1;
}
static int callback(JNIEnv* env,unsigned slot,struct AxrbArmRegisters* r) {
    uint64_t* a=r->x;
    if(a[0]!=(uint64_t)&fake_env){fail(env,"ARM JNIEnv belongs to another thread or invocation");return 0;}
    switch(slot) {
    case SLOT(FromReflectedMethod):{jobject method=(jobject)a[1];a[0]=(uint64_t)(*env)->FromReflectedMethod(env,method);if(a[0])remember_reflected(env,(jmethodID)a[0],method);break;}
    case SLOT(FromReflectedField):a[0]=(uint64_t)(*env)->FromReflectedField(env,(jobject)a[1]);break;
    case SLOT(ToReflectedMethod):a[0]=(uint64_t)(*env)->ToReflectedMethod(env,(jclass)a[1],(jmethodID)a[2],(jboolean)a[3]);break;
    case SLOT(ToReflectedField):a[0]=(uint64_t)(*env)->ToReflectedField(env,(jclass)a[1],(jfieldID)a[2],(jboolean)a[3]);break;
    case SLOT(GetSuperclass):a[0]=(uint64_t)(*env)->GetSuperclass(env,(jclass)a[1]);break;
    case SLOT(IsAssignableFrom):a[0]=(*env)->IsAssignableFrom(env,(jclass)a[1],(jclass)a[2]);break;
    case SLOT(MonitorEnter):a[0]=(uint64_t)(*env)->MonitorEnter(env,(jobject)a[1]);break;
    case SLOT(MonitorExit):a[0]=(uint64_t)(*env)->MonitorExit(env,(jobject)a[1]);break;
    case SLOT(NewWeakGlobalRef):a[0]=(uint64_t)(*env)->NewWeakGlobalRef(env,(jobject)a[1]);break;
    case SLOT(DeleteWeakGlobalRef):(*env)->DeleteWeakGlobalRef(env,(jweak)a[1]);break;
    case SLOT(GetDirectBufferAddress):a[0]=(uint64_t)(*env)->GetDirectBufferAddress(env,(jobject)a[1]);break;
    case SLOT(GetDirectBufferCapacity):a[0]=(uint64_t)(*env)->GetDirectBufferCapacity(env,(jobject)a[1]);break;
    case SLOT(NewDirectByteBuffer):a[0]=(uint64_t)(*env)->NewDirectByteBuffer(env,(void*)a[1],(jlong)a[2]);break;
    case SLOT(GetVersion):a[0]=(uint64_t)(*env)->GetVersion(env);break;
    case SLOT(FindClass):a[0]=(uint64_t)(*env)->FindClass(env,(const char*)a[1]);break;
    case SLOT(GetStaticMethodID):a[0]=(uint64_t)(*env)->GetStaticMethodID(env,(jclass)a[1],(const char*)a[2],(const char*)a[3]);remember_signature((jmethodID)a[0],(const char*)a[3]);break;
    case SLOT(GetMethodID):a[0]=(uint64_t)(*env)->GetMethodID(env,(jclass)a[1],(const char*)a[2],(const char*)a[3]);remember_signature((jmethodID)a[0],(const char*)a[3]);break;
    case SLOT(CallObjectMethod):case SLOT(CallObjectMethodV):case SLOT(CallObjectMethodA): {
        unsigned form=slot-SLOT(CallObjectMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallObjectMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallBooleanMethod):case SLOT(CallBooleanMethodV):case SLOT(CallBooleanMethodA): {
        unsigned form=slot-SLOT(CallBooleanMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallBooleanMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallByteMethod):case SLOT(CallByteMethodV):case SLOT(CallByteMethodA): {
        unsigned form=slot-SLOT(CallByteMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallByteMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallCharMethod):case SLOT(CallCharMethodV):case SLOT(CallCharMethodA): {
        unsigned form=slot-SLOT(CallCharMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallCharMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallShortMethod):case SLOT(CallShortMethodV):case SLOT(CallShortMethodA): {
        unsigned form=slot-SLOT(CallShortMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallShortMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallIntMethod):case SLOT(CallIntMethodV):case SLOT(CallIntMethodA): {
        unsigned form=slot-SLOT(CallIntMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallIntMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallLongMethod):case SLOT(CallLongMethodV):case SLOT(CallLongMethodA): {
        unsigned form=slot-SLOT(CallLongMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallLongMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallFloatMethod):case SLOT(CallFloatMethodV):case SLOT(CallFloatMethodA): {
        unsigned form=slot-SLOT(CallFloatMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        jfloat value=(*env)->CallFloatMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);memcpy(&r->v[0][0],&value,sizeof value);break;
    }
    case SLOT(CallDoubleMethod):case SLOT(CallDoubleMethodV):case SLOT(CallDoubleMethodA): {
        unsigned form=slot-SLOT(CallDoubleMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        jdouble value=(*env)->CallDoubleMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);memcpy(&r->v[0][0],&value,sizeof value);break;
    }
    case SLOT(CallVoidMethod):case SLOT(CallVoidMethodV):case SLOT(CallVoidMethodA): {
        unsigned form=slot-SLOT(CallVoidMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        (*env)->CallVoidMethodA(env,(jobject)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallStaticObjectMethod):case SLOT(CallStaticObjectMethodV):case SLOT(CallStaticObjectMethodA): {
        unsigned form=slot-SLOT(CallStaticObjectMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallStaticObjectMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallStaticBooleanMethod):case SLOT(CallStaticBooleanMethodV):case SLOT(CallStaticBooleanMethodA): {
        unsigned form=slot-SLOT(CallStaticBooleanMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallStaticBooleanMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallStaticByteMethod):case SLOT(CallStaticByteMethodV):case SLOT(CallStaticByteMethodA): {
        unsigned form=slot-SLOT(CallStaticByteMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallStaticByteMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallStaticCharMethod):case SLOT(CallStaticCharMethodV):case SLOT(CallStaticCharMethodA): {
        unsigned form=slot-SLOT(CallStaticCharMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallStaticCharMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallStaticShortMethod):case SLOT(CallStaticShortMethodV):case SLOT(CallStaticShortMethodA): {
        unsigned form=slot-SLOT(CallStaticShortMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallStaticShortMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallStaticIntMethod):case SLOT(CallStaticIntMethodV):case SLOT(CallStaticIntMethodA): {
        unsigned form=slot-SLOT(CallStaticIntMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallStaticIntMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallStaticLongMethod):case SLOT(CallStaticLongMethodV):case SLOT(CallStaticLongMethodA): {
        unsigned form=slot-SLOT(CallStaticLongMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallStaticLongMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallStaticFloatMethod):case SLOT(CallStaticFloatMethodV):case SLOT(CallStaticFloatMethodA): {
        unsigned form=slot-SLOT(CallStaticFloatMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        jfloat value=(*env)->CallStaticFloatMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);memcpy(&r->v[0][0],&value,sizeof value);break;
    }
    case SLOT(CallStaticDoubleMethod):case SLOT(CallStaticDoubleMethodV):case SLOT(CallStaticDoubleMethodA): {
        unsigned form=slot-SLOT(CallStaticDoubleMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        jdouble value=(*env)->CallStaticDoubleMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);memcpy(&r->v[0][0],&value,sizeof value);break;
    }
    case SLOT(CallStaticVoidMethod):case SLOT(CallStaticVoidMethodV):case SLOT(CallStaticVoidMethodA): {
        unsigned form=slot-SLOT(CallStaticVoidMethod);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        (*env)->CallStaticVoidMethodA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(CallNonvirtualObjectMethod):case SLOT(CallNonvirtualObjectMethodV):case SLOT(CallNonvirtualObjectMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualObjectMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallNonvirtualObjectMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);break;
    }
    case SLOT(CallNonvirtualBooleanMethod):case SLOT(CallNonvirtualBooleanMethodV):case SLOT(CallNonvirtualBooleanMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualBooleanMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallNonvirtualBooleanMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);break;
    }
    case SLOT(CallNonvirtualByteMethod):case SLOT(CallNonvirtualByteMethodV):case SLOT(CallNonvirtualByteMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualByteMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallNonvirtualByteMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);break;
    }
    case SLOT(CallNonvirtualCharMethod):case SLOT(CallNonvirtualCharMethodV):case SLOT(CallNonvirtualCharMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualCharMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallNonvirtualCharMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);break;
    }
    case SLOT(CallNonvirtualShortMethod):case SLOT(CallNonvirtualShortMethodV):case SLOT(CallNonvirtualShortMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualShortMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallNonvirtualShortMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);break;
    }
    case SLOT(CallNonvirtualIntMethod):case SLOT(CallNonvirtualIntMethodV):case SLOT(CallNonvirtualIntMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualIntMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallNonvirtualIntMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);break;
    }
    case SLOT(CallNonvirtualLongMethod):case SLOT(CallNonvirtualLongMethodV):case SLOT(CallNonvirtualLongMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualLongMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        a[0]=(uint64_t)(*env)->CallNonvirtualLongMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);break;
    }
    case SLOT(CallNonvirtualFloatMethod):case SLOT(CallNonvirtualFloatMethodV):case SLOT(CallNonvirtualFloatMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualFloatMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        jfloat value=(*env)->CallNonvirtualFloatMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);memcpy(&r->v[0][0],&value,sizeof value);break;
    }
    case SLOT(CallNonvirtualDoubleMethod):case SLOT(CallNonvirtualDoubleMethodV):case SLOT(CallNonvirtualDoubleMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualDoubleMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        jdouble value=(*env)->CallNonvirtualDoubleMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);memcpy(&r->v[0][0],&value,sizeof value);break;
    }
    case SLOT(CallNonvirtualVoidMethod):case SLOT(CallNonvirtualVoidMethodV):case SLOT(CallNonvirtualVoidMethodA): {
        unsigned form=slot-SLOT(CallNonvirtualVoidMethod);jvalue values[64];if(!call_arguments(env,r,3,form,values))return 0;
        (*env)->CallNonvirtualVoidMethodA(env,(jobject)a[1],(jclass)a[2],(jmethodID)a[3],form==2?(const jvalue*)a[4]:values);break;
    }
    case SLOT(NewObject):case SLOT(NewObjectV):case SLOT(NewObjectA): {
        unsigned form=slot-SLOT(NewObject);jvalue values[64];if(!call_arguments(env,r,2,form,values))return 0;
        a[0]=(uint64_t)(*env)->NewObjectA(env,(jclass)a[1],(jmethodID)a[2],form==2?(const jvalue*)a[3]:values);break;
    }
    case SLOT(AllocObject):a[0]=(uint64_t)(*env)->AllocObject(env,(jclass)a[1]);break;
    case SLOT(GetFieldID):a[0]=(uint64_t)(*env)->GetFieldID(env,(jclass)a[1],(const char*)a[2],(const char*)a[3]);break;
    case SLOT(GetStaticFieldID):a[0]=(uint64_t)(*env)->GetStaticFieldID(env,(jclass)a[1],(const char*)a[2],(const char*)a[3]);break;
    case SLOT(NewObjectArray):a[0]=(uint64_t)(*env)->NewObjectArray(env,(jsize)a[1],(jclass)a[2],(jobject)a[3]);break;
    case SLOT(GetObjectArrayElement):a[0]=(uint64_t)(*env)->GetObjectArrayElement(env,(jobjectArray)a[1],(jsize)a[2]);break;
    case SLOT(SetObjectArrayElement):(*env)->SetObjectArrayElement(env,(jobjectArray)a[1],(jsize)a[2],(jobject)a[3]);break;
    case SLOT(GetObjectField):a[0]=(uint64_t)(*env)->GetObjectField(env,(jobject)a[1],(jfieldID)a[2]);break;
    case SLOT(SetObjectField):(*env)->SetObjectField(env,(jobject)a[1],(jfieldID)a[2],(jobject)a[3]);break;
    case SLOT(GetBooleanField):a[0]=(uint64_t)(*env)->GetBooleanField(env,(jobject)a[1],(jfieldID)a[2]);break;
    case SLOT(SetBooleanField):(*env)->SetBooleanField(env,(jobject)a[1],(jfieldID)a[2],(jboolean)a[3]);break;
    case SLOT(GetByteField):a[0]=(uint64_t)(*env)->GetByteField(env,(jobject)a[1],(jfieldID)a[2]);break;
    case SLOT(SetByteField):(*env)->SetByteField(env,(jobject)a[1],(jfieldID)a[2],(jbyte)a[3]);break;
    case SLOT(GetCharField):a[0]=(uint64_t)(*env)->GetCharField(env,(jobject)a[1],(jfieldID)a[2]);break;
    case SLOT(SetCharField):(*env)->SetCharField(env,(jobject)a[1],(jfieldID)a[2],(jchar)a[3]);break;
    case SLOT(GetShortField):a[0]=(uint64_t)(*env)->GetShortField(env,(jobject)a[1],(jfieldID)a[2]);break;
    case SLOT(SetShortField):(*env)->SetShortField(env,(jobject)a[1],(jfieldID)a[2],(jshort)a[3]);break;
    case SLOT(GetIntField):a[0]=(uint64_t)(*env)->GetIntField(env,(jobject)a[1],(jfieldID)a[2]);break;
    case SLOT(SetIntField):(*env)->SetIntField(env,(jobject)a[1],(jfieldID)a[2],(jint)a[3]);break;
    case SLOT(GetLongField):a[0]=(uint64_t)(*env)->GetLongField(env,(jobject)a[1],(jfieldID)a[2]);break;
    case SLOT(SetLongField):(*env)->SetLongField(env,(jobject)a[1],(jfieldID)a[2],(jlong)a[3]);break;
    case SLOT(GetFloatField):{jfloat value=(*env)->GetFloatField(env,(jobject)a[1],(jfieldID)a[2]);memcpy(&r->v[0][0],&value,sizeof value);break;}
    case SLOT(SetFloatField):{jfloat value;memcpy(&value,&r->v[0][0],sizeof value);(*env)->SetFloatField(env,(jobject)a[1],(jfieldID)a[2],value);break;}
    case SLOT(GetDoubleField):{jdouble value=(*env)->GetDoubleField(env,(jobject)a[1],(jfieldID)a[2]);memcpy(&r->v[0][0],&value,sizeof value);break;}
    case SLOT(SetDoubleField):{jdouble value;memcpy(&value,&r->v[0][0],sizeof value);(*env)->SetDoubleField(env,(jobject)a[1],(jfieldID)a[2],value);break;}
    case SLOT(GetStaticObjectField):a[0]=(uint64_t)(*env)->GetStaticObjectField(env,(jclass)a[1],(jfieldID)a[2]);break;
    case SLOT(SetStaticObjectField):(*env)->SetStaticObjectField(env,(jclass)a[1],(jfieldID)a[2],(jobject)a[3]);break;
    case SLOT(GetStaticBooleanField):a[0]=(uint64_t)(*env)->GetStaticBooleanField(env,(jclass)a[1],(jfieldID)a[2]);break;
    case SLOT(SetStaticBooleanField):(*env)->SetStaticBooleanField(env,(jclass)a[1],(jfieldID)a[2],(jboolean)a[3]);break;
    case SLOT(GetStaticByteField):a[0]=(uint64_t)(*env)->GetStaticByteField(env,(jclass)a[1],(jfieldID)a[2]);break;
    case SLOT(SetStaticByteField):(*env)->SetStaticByteField(env,(jclass)a[1],(jfieldID)a[2],(jbyte)a[3]);break;
    case SLOT(GetStaticCharField):a[0]=(uint64_t)(*env)->GetStaticCharField(env,(jclass)a[1],(jfieldID)a[2]);break;
    case SLOT(SetStaticCharField):(*env)->SetStaticCharField(env,(jclass)a[1],(jfieldID)a[2],(jchar)a[3]);break;
    case SLOT(GetStaticShortField):a[0]=(uint64_t)(*env)->GetStaticShortField(env,(jclass)a[1],(jfieldID)a[2]);break;
    case SLOT(SetStaticShortField):(*env)->SetStaticShortField(env,(jclass)a[1],(jfieldID)a[2],(jshort)a[3]);break;
    case SLOT(GetStaticIntField):a[0]=(uint64_t)(*env)->GetStaticIntField(env,(jclass)a[1],(jfieldID)a[2]);break;
    case SLOT(SetStaticIntField):(*env)->SetStaticIntField(env,(jclass)a[1],(jfieldID)a[2],(jint)a[3]);break;
    case SLOT(GetStaticLongField):a[0]=(uint64_t)(*env)->GetStaticLongField(env,(jclass)a[1],(jfieldID)a[2]);break;
    case SLOT(SetStaticLongField):(*env)->SetStaticLongField(env,(jclass)a[1],(jfieldID)a[2],(jlong)a[3]);break;
    case SLOT(GetStaticFloatField):{jfloat value=(*env)->GetStaticFloatField(env,(jclass)a[1],(jfieldID)a[2]);memcpy(&r->v[0][0],&value,sizeof value);break;}
    case SLOT(SetStaticFloatField):{jfloat value;memcpy(&value,&r->v[0][0],sizeof value);(*env)->SetStaticFloatField(env,(jclass)a[1],(jfieldID)a[2],value);break;}
    case SLOT(GetStaticDoubleField):{jdouble value=(*env)->GetStaticDoubleField(env,(jclass)a[1],(jfieldID)a[2]);memcpy(&r->v[0][0],&value,sizeof value);break;}
    case SLOT(SetStaticDoubleField):{jdouble value;memcpy(&value,&r->v[0][0],sizeof value);(*env)->SetStaticDoubleField(env,(jclass)a[1],(jfieldID)a[2],value);break;}
    case SLOT(NewBooleanArray):a[0]=(uint64_t)(*env)->NewBooleanArray(env,(jsize)a[1]);break;
    case SLOT(GetBooleanArrayElements):a[0]=(uint64_t)(*env)->GetBooleanArrayElements(env,(jbooleanArray)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseBooleanArrayElements):(*env)->ReleaseBooleanArrayElements(env,(jbooleanArray)a[1],(jboolean*)a[2],(jint)a[3]);break;
    case SLOT(GetBooleanArrayRegion):(*env)->GetBooleanArrayRegion(env,(jbooleanArray)a[1],(jsize)a[2],(jsize)a[3],(jboolean*)a[4]);break;
    case SLOT(SetBooleanArrayRegion):(*env)->SetBooleanArrayRegion(env,(jbooleanArray)a[1],(jsize)a[2],(jsize)a[3],(const jboolean*)a[4]);break;
    case SLOT(NewByteArray):a[0]=(uint64_t)(*env)->NewByteArray(env,(jsize)a[1]);break;
    case SLOT(GetByteArrayElements):a[0]=(uint64_t)(*env)->GetByteArrayElements(env,(jbyteArray)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseByteArrayElements):(*env)->ReleaseByteArrayElements(env,(jbyteArray)a[1],(jbyte*)a[2],(jint)a[3]);break;
    case SLOT(GetByteArrayRegion):(*env)->GetByteArrayRegion(env,(jbyteArray)a[1],(jsize)a[2],(jsize)a[3],(jbyte*)a[4]);break;
    case SLOT(SetByteArrayRegion):(*env)->SetByteArrayRegion(env,(jbyteArray)a[1],(jsize)a[2],(jsize)a[3],(const jbyte*)a[4]);break;
    case SLOT(NewCharArray):a[0]=(uint64_t)(*env)->NewCharArray(env,(jsize)a[1]);break;
    case SLOT(GetCharArrayElements):a[0]=(uint64_t)(*env)->GetCharArrayElements(env,(jcharArray)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseCharArrayElements):(*env)->ReleaseCharArrayElements(env,(jcharArray)a[1],(jchar*)a[2],(jint)a[3]);break;
    case SLOT(GetCharArrayRegion):(*env)->GetCharArrayRegion(env,(jcharArray)a[1],(jsize)a[2],(jsize)a[3],(jchar*)a[4]);break;
    case SLOT(SetCharArrayRegion):(*env)->SetCharArrayRegion(env,(jcharArray)a[1],(jsize)a[2],(jsize)a[3],(const jchar*)a[4]);break;
    case SLOT(NewShortArray):a[0]=(uint64_t)(*env)->NewShortArray(env,(jsize)a[1]);break;
    case SLOT(GetShortArrayElements):a[0]=(uint64_t)(*env)->GetShortArrayElements(env,(jshortArray)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseShortArrayElements):(*env)->ReleaseShortArrayElements(env,(jshortArray)a[1],(jshort*)a[2],(jint)a[3]);break;
    case SLOT(GetShortArrayRegion):(*env)->GetShortArrayRegion(env,(jshortArray)a[1],(jsize)a[2],(jsize)a[3],(jshort*)a[4]);break;
    case SLOT(SetShortArrayRegion):(*env)->SetShortArrayRegion(env,(jshortArray)a[1],(jsize)a[2],(jsize)a[3],(const jshort*)a[4]);break;
    case SLOT(NewIntArray):a[0]=(uint64_t)(*env)->NewIntArray(env,(jsize)a[1]);break;
    case SLOT(GetIntArrayElements):a[0]=(uint64_t)(*env)->GetIntArrayElements(env,(jintArray)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseIntArrayElements):(*env)->ReleaseIntArrayElements(env,(jintArray)a[1],(jint*)a[2],(jint)a[3]);break;
    case SLOT(NewLongArray):a[0]=(uint64_t)(*env)->NewLongArray(env,(jsize)a[1]);break;
    case SLOT(GetLongArrayElements):a[0]=(uint64_t)(*env)->GetLongArrayElements(env,(jlongArray)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseLongArrayElements):(*env)->ReleaseLongArrayElements(env,(jlongArray)a[1],(jlong*)a[2],(jint)a[3]);break;
    case SLOT(GetLongArrayRegion):(*env)->GetLongArrayRegion(env,(jlongArray)a[1],(jsize)a[2],(jsize)a[3],(jlong*)a[4]);break;
    case SLOT(SetLongArrayRegion):(*env)->SetLongArrayRegion(env,(jlongArray)a[1],(jsize)a[2],(jsize)a[3],(const jlong*)a[4]);break;
    case SLOT(NewFloatArray):a[0]=(uint64_t)(*env)->NewFloatArray(env,(jsize)a[1]);break;
    case SLOT(GetFloatArrayElements):a[0]=(uint64_t)(*env)->GetFloatArrayElements(env,(jfloatArray)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseFloatArrayElements):(*env)->ReleaseFloatArrayElements(env,(jfloatArray)a[1],(jfloat*)a[2],(jint)a[3]);break;
    case SLOT(GetFloatArrayRegion):(*env)->GetFloatArrayRegion(env,(jfloatArray)a[1],(jsize)a[2],(jsize)a[3],(jfloat*)a[4]);break;
    case SLOT(SetFloatArrayRegion):(*env)->SetFloatArrayRegion(env,(jfloatArray)a[1],(jsize)a[2],(jsize)a[3],(const jfloat*)a[4]);break;
    case SLOT(NewDoubleArray):a[0]=(uint64_t)(*env)->NewDoubleArray(env,(jsize)a[1]);break;
    case SLOT(GetDoubleArrayElements):a[0]=(uint64_t)(*env)->GetDoubleArrayElements(env,(jdoubleArray)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseDoubleArrayElements):(*env)->ReleaseDoubleArrayElements(env,(jdoubleArray)a[1],(jdouble*)a[2],(jint)a[3]);break;
    case SLOT(GetDoubleArrayRegion):(*env)->GetDoubleArrayRegion(env,(jdoubleArray)a[1],(jsize)a[2],(jsize)a[3],(jdouble*)a[4]);break;
    case SLOT(SetDoubleArrayRegion):(*env)->SetDoubleArrayRegion(env,(jdoubleArray)a[1],(jsize)a[2],(jsize)a[3],(const jdouble*)a[4]);break;
    case SLOT(NewStringUTF):a[0]=(uint64_t)(*env)->NewStringUTF(env,(const char*)a[1]);break;
    case SLOT(GetStringUTFChars):a[0]=(uint64_t)(*env)->GetStringUTFChars(env,(jstring)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseStringUTFChars):(*env)->ReleaseStringUTFChars(env,(jstring)a[1],(const char*)a[2]);break;
    case SLOT(GetStringLength):a[0]=(uint64_t)(*env)->GetStringLength(env,(jstring)a[1]);break;
    case SLOT(GetStringUTFLength):a[0]=(uint64_t)(*env)->GetStringUTFLength(env,(jstring)a[1]);break;
    case SLOT(GetStringChars):a[0]=(uint64_t)(*env)->GetStringChars(env,(jstring)a[1],(jboolean*)a[2]);break;
    case SLOT(ReleaseStringChars):(*env)->ReleaseStringChars(env,(jstring)a[1],(const jchar*)a[2]);break;
    case SLOT(NewString):a[0]=(uint64_t)(*env)->NewString(env,(const jchar*)a[1],(jsize)a[2]);break;
    case SLOT(GetObjectClass):a[0]=(uint64_t)(*env)->GetObjectClass(env,(jobject)a[1]);break;
    case SLOT(IsInstanceOf):a[0]=(*env)->IsInstanceOf(env,(jobject)a[1],(jclass)a[2]);break;
    case SLOT(IsSameObject):a[0]=(*env)->IsSameObject(env,(jobject)a[1],(jobject)a[2]);break;
    case SLOT(NewLocalRef):a[0]=(uint64_t)(*env)->NewLocalRef(env,(jobject)a[1]);break;
    case SLOT(PushLocalFrame):a[0]=(uint64_t)(*env)->PushLocalFrame(env,(jint)a[1]);break;
    case SLOT(PopLocalFrame):a[0]=(uint64_t)(*env)->PopLocalFrame(env,(jobject)a[1]);break;
    case SLOT(ExceptionDescribe):(*env)->ExceptionDescribe(env);break;
    case SLOT(FatalError):(*env)->FatalError(env,(const char*)a[1]);return 0;
    case SLOT(Throw):a[0]=(uint64_t)(*env)->Throw(env,(jthrowable)a[1]);break;
    case SLOT(NewGlobalRef):a[0]=(uint64_t)(*env)->NewGlobalRef(env,(jobject)a[1]);break;
    case SLOT(DeleteGlobalRef):(*env)->DeleteGlobalRef(env,(jobject)a[1]);break;
    case SLOT(DeleteLocalRef):(*env)->DeleteLocalRef(env,(jobject)a[1]);break;
    case SLOT(ExceptionCheck):a[0]=(*env)->ExceptionCheck(env);break;
    case SLOT(ExceptionOccurred):a[0]=(uint64_t)(*env)->ExceptionOccurred(env);break;
    case SLOT(ExceptionClear):(*env)->ExceptionClear(env);break;
    case SLOT(ThrowNew):a[0]=(uint64_t)(*env)->ThrowNew(env,(jclass)a[1],(const char*)a[2]);break;
    case SLOT(GetArrayLength):a[0]=(uint64_t)(*env)->GetArrayLength(env,(jarray)a[1]);break;
    case SLOT(GetIntArrayRegion):(*env)->GetIntArrayRegion(env,(jintArray)a[1],(jsize)a[2],(jsize)a[3],(jint*)a[4]);break;
    case SLOT(SetIntArrayRegion):(*env)->SetIntArrayRegion(env,(jintArray)a[1],(jsize)a[2],(jsize)a[3],(const jint*)a[4]);break;
    case SLOT(GetJavaVM):*(uint64_t*)a[1]=(uint64_t)&fake_vm;a[0]=JNI_OK;break;
    case SLOT(RegisterNatives): {
        int count=(int)a[3];const JNINativeMethod* input=(const JNINativeMethod*)a[2];
        if(count<=0 || count>MAX_METHODS){fail(env,"invalid ARM RegisterNatives count");return 0;}
        JNINativeMethod native[MAX_METHODS];
        for(int i=0;i<count;i++) {
            char descriptor[64];native[i]=input[i];
            if(!shorty(input[i].signature,descriptor)){fail(env,"invalid ARM JNI signature");return 0;}
            native[i].fnPtr=pointer_trampoline((uint64_t)input[i].fnPtr,descriptor,(unsigned)strlen(descriptor),0);
            if(!native[i].fnPtr){fail(env,"ARM RegisterNatives trampoline unavailable");return 0;}
        }
        a[0]=(uint64_t)(*env)->RegisterNatives(env,(jclass)a[1],native,count);break;
    }
    default: {char message[96];snprintf(message,sizeof message,"ARM JNI slot %u is not implemented",slot);fail(env,message);return 0;}
    }
    return 1;
}
void axrb_arm_dispatch(unsigned slot,struct Frame* frame) {
    frame->result=frame->fp_result=0;
    if(slot>=method_count)return;
    JNIEnv* env=0;
    if(methods[slot].vm_entry) {
        JavaVM* vm=(JavaVM*)frame->gp[0];
        if((*vm)->GetEnv(vm,(void**)&env,JNI_VERSION_1_6)!=JNI_OK){frame->result=(uint64_t)JNI_ERR;return;}
    } else env=(JNIEnv*)frame->gp[0];
    pthread_once(&tables_once,init_tables);fake_env=(uint64_t)jni_table;
    if(!fake_vm){fail(env,"ARM JNI table initialization failed");return;}
    long gates=transport(AXRB_ARM_GATES,0,0);if(gates<0){fail(env,"ARM gateway unavailable");return;}
    void* stack=acquire_call_stack();if(!stack){fail(env,"ARM stack allocation failed or nesting limit exceeded");return;}
    struct AxrbArmRegisters r={0};r.pc=methods[slot].pc;r.x[0]=methods[slot].vm_entry?(uint64_t)&fake_vm:(uint64_t)&fake_env;r.x[1]=frame->gp[1];r.x[30]=(uint64_t)gates;
    r.sp=((uint64_t)stack+ARM_CALL_STACK_SIZE-512)&~(uint64_t)15;
    unsigned x=2,v=0,host_gp=2,host_fp=0,host_sp=0,arm_sp=0;
    for(const char* t=methods[slot].shorty+1;*t;t++) {
        int floating=*t=='F' || *t=='D';uint64_t value;
        if(floating && host_fp<8)value=frame->fp[host_fp++][0];
        else if(!floating && host_gp<6)value=frame->gp[host_gp++];
        else value=((uint64_t*)frame->stack)[host_sp++];
        if(floating && v<8) r.v[v++][0]=value;
        else if(!floating && x<8)r.x[x++]=value;
        else ((uint64_t*)r.sp)[arm_sp++]=value;
    }
    long invocation=transport(AXRB_ARM_BEGIN,(long)r.pc,0);
    if(invocation<0){--call_depth;fail(env,"ARM call initialization failed");return;}
    for(unsigned budget=0;budget<100000;budget++) {
        long result=transport(AXRB_ARM_STEP,invocation,(long)&r);
        if(result==1){frame->result=r.x[0];frame->fp_result=r.v[0][0];break;}
        if(result>=0x100 && result<0x200){if(!callback(env,(unsigned)result-0x100,&r))break;}
        else if(result==0x380) {
            if(r.x[0]!=(uint64_t)&fake_env){fail(env,"invalid JNIEnv for ARM native window");break;}
            r.x[0]=(uint64_t)ANativeWindow_fromSurface(env,(jobject)r.x[1]);
        }
        else if(result==0x381)ANativeWindow_acquire((ANativeWindow*)r.x[0]);
        else if(result==0x382)ANativeWindow_release((ANativeWindow*)r.x[0]);
        else if(result==0x383)r.x[0]=(uint64_t)ANativeWindow_getWidth((ANativeWindow*)r.x[0]);
        else if(result==0x384)r.x[0]=(uint64_t)ANativeWindow_getHeight((ANativeWindow*)r.x[0]);
        else if(result==0x385)r.x[0]=(uint64_t)ANativeWindow_setBuffersGeometry((ANativeWindow*)r.x[0],(int32_t)r.x[1],(int32_t)r.x[2],(int32_t)r.x[3]);
        else if(result==0x304 || result==0x307) {
            // This gateway is entered from a JNI call on an already attached
            // ART thread. Do not create a second Java thread or JNIEnv.
            if(r.x[0]!=(uint64_t)&fake_vm || !r.x[1]){fail(env,"invalid ARM VM attachment request");break;}
            *(uint64_t*)r.x[1]=(uint64_t)&fake_env;r.x[0]=JNI_OK;
        }
        else if(result==0x306) { // JavaVM::GetEnv on an already attached ART thread.
            if(r.x[0]!=(uint64_t)&fake_vm){fail(env,"invalid ARM JavaVM");break;}
            jint version=(jint)r.x[2];
            if(version==JNI_VERSION_1_2 || version==JNI_VERSION_1_4 || version==JNI_VERSION_1_6) {
                *(uint64_t*)r.x[1]=(uint64_t)&fake_env;r.x[0]=JNI_OK;
            } else {*(uint64_t*)r.x[1]=0;r.x[0]=(uint64_t)JNI_EVERSION;}
        }
        else if(result!=0){fail(env,"ARM execution failed; see runtime diagnostic");break;}
        if(budget==99999)fail(env,"ARM call exceeded execution budget");
    }
    transport(AXRB_ARM_END,invocation,0);--call_depth;
}
