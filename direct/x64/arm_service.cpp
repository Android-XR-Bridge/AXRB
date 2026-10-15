#define AXRB_ARM_SERVICE_BUILD
#include "arm_service.h"
#include "cpu.h"
#include "exec.h"
#include "files.h"
#include "kernel.h"
#include "log.h"
#include "memory.h"
#include "net.h"
#include "props.h"
#include "sysroot.h"
#include "vfs.h"
#include "ndk.h"
#include "media.h"
#include "audio.h"
#include "vk_host.h"
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <array>
#include <filesystem>
#include "arun/zip_reader.h"
#include <stdexcept>
using namespace axrb;
namespace {
struct Ready{};
struct Yield{int code;};
thread_local bool translating=false;
thread_local uint64_t return_gate=0;
struct Service {uint64_t load,symbol,error,worker;} api{};
GuestThread* main_thread=nullptr;
DWORD owner=0;
struct Worker {
    std::mutex mutex;std::condition_variable changed;bool online=false,pending=false,done=false,assigned=false;
    AxrbArmRegisters* registers=nullptr;uint64_t gate=0;int result=-1;std::string error;
    unsigned operation=0;const char* name=nullptr;uint64_t handle=0,value=0;
};
std::array<Worker,16> workers;
thread_local Worker* assigned_worker=nullptr;

bool attempted=false,ready=false;
std::mutex lock;
thread_local std::string error;
bool on_owner(){if(!ready || owner!=GetCurrentThreadId()){error="ARM service is not initialized on this host thread";return false;}return true;}
void remember_error(GuestThread& thread){auto p=call_guest(thread,api.error,{});if(!p || !guest_string(p,error,4096))error="ARM dlopen/dlsym failed without dlerror";}
}
namespace axrb {
bool arm_service_named(GuestThread&,const std::string&);
void arm_service_trap(GuestThread&,uint32_t immediate,uint64_t pc) {
    if(translating && ((immediate==1 && pc==return_gate) || (immediate>=0x100 && immediate<0x400)))throw Yield{static_cast<int>(immediate)};
}
}
AXRB_SERVICE int64_t axrb_arm_service_host(const char* name,const uint64_t* args) {
    std::lock_guard guard(lock);if(!on_owner() || !name || !args)return -1;
    CpuContext saved;save_context(*main_thread,saved);
    for(int i=0;i<5;i++)set_reg(*main_thread,i,args[i]);
    int64_t result=-38;
    try{if(arm_service_named(*main_thread,name))result=static_cast<int64_t>(get_reg(*main_thread,0));}
    catch(const ThreadExit&){result=-4;}catch(const std::exception& e){error=e.what();result=-5;}
    load_context(*main_thread,saved);return result;
}
AXRB_SERVICE int axrb_arm_service_is_function(uint64_t pc){return ready && address_space().check(pc,4,linux_abi::PROT_EXEC_);}
static int execute_step(GuestThread& thread,AxrbArmRegisters* r,uint64_t gate) {
    CpuContext saved,context{};save_context(thread,saved);
    std::memcpy(context.x,r->x,sizeof context.x);std::memcpy(context.v,r->v,sizeof r->v);
    context.pc=r->pc;context.sp=r->sp;context.pstate=r->pstate;context.fpcr=r->fpcr;context.fpsr=r->fpsr;
    load_context(thread,context);translating=true;return_gate=gate;
    int result=-1;
    try{run_guest(thread,false);}catch(const Yield& y){result=y.code;}
    catch(const ThreadExit& e){error="ARM call exited: "+std::to_string(e.code);ready=false;}
    catch(const std::exception& e){error=e.what();}
    translating=false;save_context(thread,context);load_context(thread,saved);
    std::memcpy(r->x,context.x,sizeof r->x);std::memcpy(r->v,context.v,sizeof r->v);
    r->pc=context.pc;r->sp=context.sp;r->pstate=context.pstate;r->fpcr=context.fpcr;r->fpsr=context.fpsr;r->tls=thread.tpidr;
    return result;
}
static Worker* get_worker() {
    if(!assigned_worker) {
        std::lock_guard guard(lock);
        for(auto& worker:workers)if(!worker.assigned){worker.assigned=true;assigned_worker=&worker;break;}
        if(!assigned_worker)error="ARM worker pool exhausted";
    }
    return assigned_worker;
}
AXRB_SERVICE int axrb_arm_service_step(AxrbArmRegisters* r,uint64_t gate) {
    if(!ready || !r)return -1;
    if(owner==GetCurrentThreadId()) {std::lock_guard guard(lock);return execute_step(*main_thread,r,gate);}
    auto* selected=get_worker();if(!selected)return -1;
    auto& worker=*selected;std::unique_lock guard(worker.mutex);
    worker.operation=1;worker.registers=r;worker.gate=gate;worker.pending=true;worker.done=false;worker.changed.notify_all();
    worker.changed.wait(guard,[&]{return worker.done;});error=worker.error;return worker.result;
}
static uint64_t worker_link(unsigned operation,uint64_t handle,const char* name) {
    auto* selected=get_worker();if(!selected)return 0;
    auto& worker=*selected;std::unique_lock guard(worker.mutex);
    worker.operation=operation;worker.name=name;worker.handle=handle;worker.pending=true;worker.done=false;worker.changed.notify_all();
    worker.changed.wait(guard,[&]{return worker.done;});error=worker.error;return worker.value;
}
AXRB_SERVICE const char* axrb_arm_service_error(){return error.c_str();}
AXRB_SERVICE int64_t axrb_arm_service_binder(uint32_t op,int64_t fd,uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
    if(!ready)return -19;
    // Binder's 64-bit UAPI is shared by AArch64 and x86_64. Its waiters require
    // kernel thread metadata, but no ARM CPU or Bionic TLS on x86 Binder workers.
    auto* thread=current_thread();
    if(!thread) {
        thread=new GuestThread();thread->tid=process().next_tid.fetch_add(1);
        thread->host_tid=GetCurrentThreadId();thread->name="x64-binder";
        register_main_thread(thread);set_current_thread(thread);
    }
    try {
        if(op==0) {
            const char* path="/dev/binder";
            return do_syscall(*thread,56,uint64_t(-100),reinterpret_cast<uint64_t>(path),a,0,0,0);
        }
        if(op==1)return do_syscall(*thread,29,fd,a,b,0,0,0);
        if(op==2)return do_syscall(*thread,222,0,a,b,c,fd,d);
        if(op==3)return do_syscall(*thread,57,fd,0,0,0,0,0);
    }catch(const ThreadExit&){return -4;}catch(const std::exception& e){error=e.what();return -5;}
    return -22;
}
AXRB_SERVICE int axrb_arm_service_start(const AxrbArmServiceConfig* config) {
    std::lock_guard guard(lock);
    if(!config || config->size!=sizeof(*config) || config->version!=1 || !config->sysroot || !config->helper || !config->libraries){error="invalid ARM service configuration";return -1;}
    if(attempted){error="ARM service initialization already attempted";return -1;}attempted=true;owner=GetCurrentThreadId();
    try {
        log_init();if(!address_space().init(false))throw std::runtime_error("ARM address space initialization failed");
        install_fault_handler();net_init();
        if(!sysroot_init(config->sysroot))throw std::runtime_error("ARM sysroot initialization failed");
        proc_init();
        std::map<std::string,std::string> overrides;
        if(vulkan_prepare_gpu_share())overrides.emplace("debug.axrb.gpu_share","1");
        props_init(config->sysroot,overrides);
        art_windows_init();art_media_init();audio_init();vulkan_init();
        wchar_t data_root[32768]{};
        auto data_length=GetEnvironmentVariableW(L"AXRB_X64_DATA",data_root,32768);
        if(data_length && data_length<32768) {
            std::filesystem::path data(data_root);
            if(!std::filesystem::is_directory(data))throw std::runtime_error("Shared Android data directory does not exist");
            vfs().add_mount("/data",data.wstring(),false);
            auto storage=data.parent_path()/L"storage";
            if(std::filesystem::is_directory(storage))vfs().add_mount("/storage",storage.wstring(),false);
            wchar_t apk_path[32768]{};
            auto apk_length=GetEnvironmentVariableW(L"AXRB_X64_APK",apk_path,32768);
            if(apk_length && apk_length<32768) {
                auto apk=arun::pkg::ZipReader::Open(apk_path);
                if(!apk)throw std::runtime_error("Cannot open native ART application APK");
                NdkConfig ndk;ndk.apk=std::make_shared<arun::pkg::ZipReader>(std::move(*apk));
                // The harness stages the APK at the same path in both ISA views.
                ndk.guest_apk="/data/local/tmp/native-game/base.apk";ndk_init(ndk);
            }
        }
        register_named_function("art.stop_requested",[](HostCall& call){call.ret(0);});
        vfs().add_mount("/data/local/tmp/native-arm-helper",config->helper,true,true);
        vfs().add_mount("/data/local/tmp/native-arm",config->libraries,true);
        vfs().add_mount("/data/app/axrb/lib/arm64",config->libraries,true);
        Process& proc=process();proc.pid=4242;proc.next_tid=4243;proc.cwd="/";
        main_thread=new GuestThread();main_thread->tid=proc.pid;main_thread->host_tid=owner;main_thread->name="native-arm";
        register_main_thread(main_thread);set_current_thread(main_thread);
        fds().install_at(0,std::make_shared<ConsoleFile>(0),false);
        fds().install_at(1,std::make_shared<ConsoleFile>(1),false);
        fds().install_at(2,std::make_shared<ConsoleFile>(2),false);
        register_named_function("native_bridge.ready",[](HostCall& call){
            if(!guest_read(call.x(0),&api,sizeof api) || !api.load || !api.symbol || !api.error)throw std::runtime_error("invalid ARM service handshake");
            throw Ready{};
        });
        register_named_function("native_bridge.worker",[](HostCall& call){
            auto* worker=reinterpret_cast<Worker*>(call.x(0));
            bool known=false;for(auto& candidate:workers)if(&candidate==worker)known=true;
            if(!known)throw ThreadExit{1};
            std::unique_lock guard(worker->mutex);worker->online=true;worker->changed.notify_all();
            for(;;) {
                worker->changed.wait(guard,[&]{return worker->pending;});
                worker->pending=false;guard.unlock();
                int result=0;uint64_t value=0;
                try {
                    if(worker->operation==1)result=execute_step(call.thread(),worker->registers,worker->gate);
                    else {
                        value=worker->operation==2?call_guest(call.thread(),api.load,{reinterpret_cast<uint64_t>(worker->name)}):call_guest(call.thread(),api.symbol,{worker->handle,reinterpret_cast<uint64_t>(worker->name)});
                        if(!value)remember_error(call.thread());else error.clear();
                    }
                }catch(const ThreadExit& e){error="ARM worker exited: "+std::to_string(e.code);result=-1;}
                catch(const std::exception& e){error=e.what();result=-1;}
                worker->value=value;
                guard.lock();worker->result=result;worker->error=error;worker->done=true;worker->changed.notify_all();
            }
        });
        CpuContext context;
        std::vector<std::string> env={"ANDROID_ROOT=/system","ANDROID_DATA=/data",
            "LD_LIBRARY_PATH=/apex/com.android.runtime/lib64/bionic:/system/lib64"};
        if(exec_program(*main_thread,"/data/local/tmp/native-arm-helper",{"native-arm-helper"},env,context))throw std::runtime_error("ARM helper exec failed");
        attach_cpu(*main_thread);load_context(*main_thread,context);
        try{run_guest(*main_thread,false);}catch(const Ready&){ready=true;}
        if(!ready)throw std::runtime_error("ARM helper did not complete initialization");
        for(auto& worker:workers) {
            if(call_guest(*main_thread,api.worker,{reinterpret_cast<uint64_t>(&worker)}))throw std::runtime_error("ARM pthread worker creation failed");
            std::unique_lock guard(worker.mutex);
            if(!worker.changed.wait_for(guard,std::chrono::seconds(5),[&]{return worker.online;}))throw std::runtime_error("ARM pthread worker did not start");
        }
        error.clear();return 0;
    }catch(const ThreadExit& e){error="ARM service exited during initialization: "+std::to_string(e.code);}
    catch(const std::exception& e){error=e.what();}
    return -1;
}
AXRB_SERVICE uint64_t axrb_arm_service_load(const char* path) {
    if(ready && path && owner!=GetCurrentThreadId())return worker_link(2,0,path);
    std::lock_guard guard(lock);if(!on_owner() || !path)return 0;
    try{auto handle=call_guest(*main_thread,api.load,{reinterpret_cast<uint64_t>(path)});if(!handle)remember_error(*main_thread);else error.clear();return handle;}
    catch(const ThreadExit& e){error="ARM load exited: "+std::to_string(e.code);ready=false;}
    catch(const std::exception& e){error=e.what();}return 0;
}
AXRB_SERVICE uint64_t axrb_arm_service_symbol(uint64_t handle,const char* name) {
    if(ready && handle && name && owner!=GetCurrentThreadId())return worker_link(3,handle,name);
    std::lock_guard guard(lock);if(!on_owner() || !handle || !name)return 0;
    try{auto address=call_guest(*main_thread,api.symbol,{handle,reinterpret_cast<uint64_t>(name)});if(!address)remember_error(*main_thread);else error.clear();return address;}
    catch(const ThreadExit& e){error="ARM dlsym exited: "+std::to_string(e.code);ready=false;}
    catch(const std::exception& e){error=e.what();}return 0;
}
