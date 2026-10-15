// Native ART <-> translated ARM execution transport. All calls are made under
// the x64 kernel-state mutex; Java callbacks occur after returning to x64 ELF.
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include "arm_bridge.h"
#include "arm_elf.h"
#include "arm_service.h"
#include "files.h"
#include "arm_bridge_abi.h"
#include "../digitalis/digitalis.h"
namespace arm_bridge {
namespace dg=axrb::digitalis;
namespace {
using Library=arm_elf::Image;
std::vector<std::unique_ptr<Library>> libraries;
Access guest_access=nullptr;
struct Embedded {
    HMODULE module=nullptr;
    decltype(&axrb_arm_service_start) start=nullptr;
    decltype(&axrb_arm_service_load) load=nullptr;
    decltype(&axrb_arm_service_symbol) symbol=nullptr;
    decltype(&axrb_arm_service_step) step=nullptr;
    decltype(&axrb_arm_service_is_function) is_function=nullptr;
    decltype(&axrb_arm_service_error) error=nullptr;
    decltype(&axrb_arm_service_binder) binder=nullptr;
    decltype(&axrb_arm_service_host) host=nullptr;
    bool ready=false;
    std::vector<uint64_t> handles;
    bool enabled() const {return GetEnvironmentVariableW(L"AXRB_X64_ARM_SERVICE",nullptr,0)!=0;}
    void preload() {
        if(module)return;
        wchar_t dll[32768],root[32768],helper[32768],libs[32768];
        auto env=[](const wchar_t* key,wchar_t* value){auto n=GetEnvironmentVariableW(key,value,32768);if(!n || n>=32768)throw std::runtime_error("missing/oversized ARM service environment path");};
        env(L"AXRB_X64_ARM_SERVICE",dll);env(L"AXRB_X64_ARM_SYSROOT",root);env(L"AXRB_X64_ARM_HELPER",helper);env(L"AXRB_X64_ARM_LIBRARIES",libs);
        module=LoadLibraryExW(dll,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!module)throw std::runtime_error("cannot load embedded ARM service DLL: Windows error "+std::to_string(GetLastError()));
#define SERVICE_FN(field,name) field=reinterpret_cast<decltype(field)>(GetProcAddress(module,name));if(!field)throw std::runtime_error("ARM service ABI missing " name)
        SERVICE_FN(start,"axrb_arm_service_start");SERVICE_FN(load,"axrb_arm_service_load");SERVICE_FN(symbol,"axrb_arm_service_symbol");
        SERVICE_FN(step,"axrb_arm_service_step");SERVICE_FN(is_function,"axrb_arm_service_is_function");SERVICE_FN(error,"axrb_arm_service_error");SERVICE_FN(binder,"axrb_arm_service_binder");SERVICE_FN(host,"axrb_arm_service_host");
#undef SERVICE_FN
    }
    void init() {
        if(ready)return;preload();
        wchar_t root[32768],helper[32768],libs[32768];
        GetEnvironmentVariableW(L"AXRB_X64_ARM_SYSROOT",root,32768);GetEnvironmentVariableW(L"AXRB_X64_ARM_HELPER",helper,32768);GetEnvironmentVariableW(L"AXRB_X64_ARM_LIBRARIES",libs,32768);
        AxrbArmServiceConfig config{sizeof(config),1,root,helper,libs};
        if(start(&config))throw std::runtime_error(error());ready=true;
        std::fprintf(stderr,"AXRB_NATIVE_ARM_SERVICE_READY real ARM linker/Bionic/TLS\n");
    }
} embedded;
uint32_t* gates=nullptr;
bool arm_access(uint64_t address,uint64_t length,uint32_t flags) {
    if(address>UINT64_MAX-length) return false;
    if(gates && address>=reinterpret_cast<uint64_t>(gates) && address+length<=reinterpret_cast<uint64_t>(gates)+4096 && !(flags&2)) return true;
    for(const auto& library:libraries) if(!(flags&1) || library->callable) for(const auto& area:library->areas)
        if(address>=area.begin && address+length<=area.end && (area.flags&flags)==flags) return true;
    return false;
}
bool read(void*,uint64_t address,void* out,size_t size) {
    if(!arm_access(address,size,4) && !guest_access(address,size,false)) return false;
    std::memcpy(out,reinterpret_cast<void*>(address),size);return true;
}
bool write(void*,uint64_t address,const void* data,size_t size) {
    if(!arm_access(address,size,2) && !guest_access(address,size,true)) return false;
    std::memcpy(reinterpret_cast<void*>(address),data,size);return true;
}
bool fetch(void*,uint64_t address,void* out,size_t size) {
    if(!arm_access(address,size,1)) return false;
    std::memcpy(out,reinterpret_cast<void*>(address),size);return true;
}
struct Engine {
    dg::Engine* engine=dg::create({nullptr,read,write,nullptr,nullptr,fetch});
    bool busy=false;
    ~Engine(){dg::destroy(engine);}
};
thread_local std::vector<std::unique_ptr<Engine>> engines;
struct Invocation {Engine* engine;DWORD owner;};
std::unordered_map<uint64_t,Invocation> invocations;
uint64_t next_invocation=1;
static_assert(sizeof(AxrbArmRegisters)==sizeof(dg::Registers));
static_assert(offsetof(AxrbArmRegisters,v)==offsetof(dg::Registers,v));
struct Group { std::vector<uint64_t> constructors; size_t next=0; size_t begin,end; DWORD owner=GetCurrentThreadId(); bool failed=false; };
std::unordered_map<uint64_t,Group> groups;
uint64_t publish(std::vector<std::unique_ptr<Library>> images) {
    if(libraries.size()+images.size()>256)throw std::runtime_error("ARM library limit exceeded");
    Group group;group.begin=libraries.size();group.end=group.begin+images.size();
    std::vector<Library*> visited;
    std::function<void(Library*)> visit=[&](Library* image) {
        if(std::find(visited.begin(),visited.end(),image)!=visited.end())return;
        visited.push_back(image);for(auto* dependency:image->dependencies)visit(dependency);
        group.constructors.insert(group.constructors.end(),image->initializers.begin(),image->initializers.end());
    };
    visit(images.front().get());auto handle=libraries.size()+1;
    libraries.reserve(group.end);groups.emplace(handle,std::move(group));
    for(auto& image:images)libraries.push_back(std::move(image));
    return handle;
}
std::string guest_string(uint64_t address,Access access) {
    std::string result;
    for(size_t i=0;i<4096;i++){if(address>UINT64_MAX-i || !access(address+i,1,false))throw std::runtime_error("invalid ARM path pointer");char value=*reinterpret_cast<char*>(address+i);if(!value)return result;result+=value;}
    throw std::runtime_error("ARM path exceeds limit");
}

}
int64_t binder(uint32_t op,int64_t fd,uint64_t a,uint64_t b,uint64_t c,uint64_t d){try{if(!embedded.enabled())return -19;embedded.init();return embedded.binder(op,fd,a,b,c,d);}catch(const std::exception& e){std::fprintf(stderr,"ARM Binder service: %s\n",e.what());return -5;}}
int64_t platform_host(const char* name,const uint64_t* args){if(!embedded.ready)return -19;return embedded.host(name,args);}
void prepare(){if(embedded.enabled())embedded.preload();}
int64_t dispatch(uint64_t operation,uint64_t a,uint64_t b,uint64_t version,Access access,ReadOnlyFiles* files,void(*unlock)(),void(*relock)()) {
    if(version!=AXRB_ARM_ABI) return -22;guest_access=access;
    try {
        if(!gates) {
            gates=static_cast<uint32_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
            if(!gates) return -12;
            gates[0]=0xd4000021;gates[1]=0xd65f03c0; // SVC #1: return sentinel.
            for(unsigned i=0;i<256;i++){gates[2+i*2]=0xd4000001|((0x100+i)<<5);gates[3+i*2]=0xd65f03c0;}
            for(unsigned i=0;i<8;i++){gates[514+i*2]=0xd4000001|((0x300+i)<<5);gates[515+i*2]=0xd65f03c0;}
            DWORD old;VirtualProtect(gates,4096,PAGE_READONLY,&old);
        }
        if(operation==AXRB_ARM_GATES) return reinterpret_cast<uint64_t>(gates);
        if(operation==AXRB_ARM_IS_FUNCTION) return arm_access(a,4,1) || (embedded.ready && embedded.is_function(a))?1:0;
        if(operation==AXRB_ARM_LOAD) {
            if(b>512ull*1024*1024 || !access(a,b,false))return -14;
            auto images=arm_elf::load(reinterpret_cast<uint8_t*>(a),static_cast<size_t>(b),"<memory>",{});
            // The legacy buffer operation has no initializer handshake.
            for(auto& image:images)if(!image->initializers.empty())throw std::runtime_error("ARM constructors require path loading");
            auto id=publish(std::move(images));for(size_t i=groups[id].begin;i<groups[id].end;i++)libraries[i]->active=true;return id;
        }
        if(operation==AXRB_ARM_LOAD_PATH) {
            if(!files)return -38;auto path=guest_string(a,access);
            if(path.empty() || path[0]!='/')return -22;
            if(embedded.enabled()) {
                  embedded.init();
                  // Keep Java and native dlopen on the same guest pathname:
                  // remapping by basename creates a second plugin instance.
                  auto handle=embedded.load(path.c_str());
                if(!handle)throw std::runtime_error(embedded.error());embedded.handles.push_back(handle);return static_cast<int64_t>(embedded.handles.size());
            }
            auto reader=[&](const std::string& requested) {
                std::vector<uint8_t> bytes;int fd=files->open(requested,0);if(fd<0)return bytes;
                LinuxStat st{};if(files->stat(fd,st) || st.size<=0 || st.size>512ll*1024*1024){files->close(fd);return bytes;}
                try {bytes.resize(static_cast<size_t>(st.size));}catch(...){files->close(fd);throw;}
                auto count=files->read(fd,bytes.data(),bytes.size(),0,false);files->close(fd);if(count!=st.size)bytes.clear();return bytes;
            };
            auto bytes=reader(path);if(bytes.empty())return -2;
            return publish(arm_elf::load(bytes.data(),bytes.size(),path,reader));
        }
        if(operation==AXRB_ARM_INITIALIZER) {
            if(embedded.ready)return b?-8:0;
            auto it=groups.find(a);if(it==groups.end())return -9;auto& group=it->second;
            if(group.owner!=GetCurrentThreadId())return -9;
            if(b){group.failed=true;for(size_t i=group.begin;i<group.end;i++)libraries[i]->callable=false;return -8;}if(group.failed)return -8;
            if(group.next<group.constructors.size())return group.constructors[group.next++];
            for(size_t i=group.begin;i<group.end;i++)libraries[i]->active=true;
            return 0;
        }
        if(operation==AXRB_ARM_SYMBOL) {
            if(embedded.ready){if(!a || a>embedded.handles.size())return -9;auto name=guest_string(b,access);auto pc=embedded.symbol(embedded.handles[a-1],name.c_str());return pc?static_cast<int64_t>(pc):-2;}
            if(!a || a>libraries.size() || !libraries[a-1]->active) return -9;std::string name;
            for(size_t i=0;i<1024;i++){if(!access(b+i,1,false)) return -14;char v=*reinterpret_cast<char*>(b+i);if(!v) {auto it=libraries[a-1]->symbols.find(name);return it==libraries[a-1]->symbols.end()?-2:static_cast<int64_t>(it->second);}name+=v;}
            return -36;
        }
        if(operation==AXRB_ARM_BEGIN) {
            if(embedded.ready && embedded.is_function(a)){auto id=next_invocation++;invocations.emplace(id,Invocation{nullptr,GetCurrentThreadId()});return id;}
            if(!arm_access(a,4,1)) return -14;
            Engine* selected=nullptr;for(auto& engine:engines) if(!engine->busy){selected=engine.get();break;}
            if(!selected) {if(engines.size()>=16) return -12;engines.push_back(std::make_unique<Engine>());selected=engines.back().get();}
            selected->busy=true;auto id=next_invocation++;invocations.emplace(id,Invocation{selected,GetCurrentThreadId()});return id;
        }
        auto it=invocations.find(a);if(it==invocations.end() || it->second.owner!=GetCurrentThreadId()) return -9;
        if(operation==AXRB_ARM_END) {if(it->second.engine)it->second.engine->busy=false;invocations.erase(it);return 0;}
        if(operation!=AXRB_ARM_STEP) return -22;
        if(!access(b,sizeof(AxrbArmRegisters),true)) return -14;
        if(!it->second.engine){
            struct Relock {void(*fn)();~Relock(){if(fn)fn();}} guard{relock};
            if(unlock)unlock();
            auto result=embedded.step(reinterpret_cast<AxrbArmRegisters*>(b),reinterpret_cast<uint64_t>(gates));
            if(result<0)std::fprintf(stderr,"ARM service: %s\n",embedded.error());return result;
        }
        dg::Registers registers;std::memcpy(&registers,reinterpret_cast<void*>(b),sizeof registers);
        auto result=dg::run(it->second.engine->engine,&registers,128);
        std::memcpy(reinterpret_cast<void*>(b),&registers,sizeof registers);
        if(result.reason==dg::Stop::Yield) return 0;
        if(result.reason==dg::Stop::Svc) return result.immediate?result.immediate:-38;
        std::fprintf(stderr,"ARM bridge fault reason=%u pc=%llx address=%llx\n",static_cast<unsigned>(result.reason),registers.pc,result.address);return -8;
    } catch(const std::exception& error) {std::fprintf(stderr,"ARM bridge: %s\n",error.what());return -8;}
}
}
