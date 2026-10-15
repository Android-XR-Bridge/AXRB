// Native x86_64 execution boundary prototype. No hypervisor and no ARM CPU engine.
// Trusted developer ELF/ART execution only; partial Linux ABI, not a sandbox.
#include <windows.h>
#include <bcrypt.h>
#include <immintrin.h>
#include <Zydis/Zydis.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <mutex>
#include <chrono>
#include <memory>
#include <atomic>
#include "files.h"
#include "tls_bridge.h"
#include "arm_bridge.h"
#include "arm_bridge_abi.h"
#pragma comment(lib,"bcrypt.lib")
#if !defined(_M_X64)
#error This native execution prototype requires an x86_64 Windows compiler target.
#endif

struct Header {
    uint8_t ident[16]; uint16_t type,machine; uint32_t version;
    uint64_t entry,phoff,shoff; uint32_t flags; uint16_t size,phsize,phnum,shsize,shnum,shstr;
};
struct Program { uint32_t type,flags; uint64_t offset,address,physical,filesize,memsize,align; };
struct Section { uint32_t name,type; uint64_t flags,address,offset,size; uint32_t link,info; uint64_t align,entsize; };
static_assert(sizeof(Header)==64 && sizeof(Program)==56 && sizeof(Section)==64);
constexpr uint64_t page=4096;
uint64_t up(uint64_t v) { if(v>UINT64_MAX-page+1) throw std::runtime_error("ELF size overflow"); return (v+page-1)&~(page-1); }
struct Range { uintptr_t begin,end; uint32_t prot; };
struct CodePatch { uint64_t offset,address; bool gs; std::vector<uint8_t> instruction; };
bool patch_gs(uint8_t* gate,const CodePatch& patch,native_tls::Batch& bridges) {
    auto* stub=bridges.generate(patch.instruction,reinterpret_cast<uintptr_t>(gate)+patch.instruction.size());
    if(!stub) return false;
    int64_t displacement=reinterpret_cast<int64_t>(stub)-reinterpret_cast<int64_t>(gate)-5;
    if(displacement<INT32_MIN || displacement>INT32_MAX) throw std::runtime_error("GS bridge is outside branch range");
    std::memset(gate,0x90,patch.instruction.size());gate[0]=0xe9;
    int32_t relative=static_cast<int32_t>(displacement);std::memcpy(gate+1,&relative,4);return true;
}
struct Image {
    std::vector<uint8_t> bytes;
    Header h{};
    std::vector<Program> programs;
    std::vector<Section> sections;
    std::string interpreter;
    bool approved=false;
    uint8_t* memory=nullptr;
    uint64_t span=0;
    std::unordered_set<uintptr_t> gates;
    std::unordered_map<uintptr_t,std::vector<uint8_t>> gs_gates;
    std::vector<Section> code_sections;
    std::vector<Range> ranges;
    bool within(uint64_t off,uint64_t size) const { return off<=bytes.size() && size<=bytes.size()-off; }
    template<class T> T get(uint64_t off) const {
        if(!within(off,sizeof(T))) throw std::runtime_error("Truncated ELF structure");
        T value; std::memcpy(&value,bytes.data()+off,sizeof value); return value;
    }
    explicit Image(const wchar_t* filename) {
        std::ifstream file(std::filesystem::path(filename),std::ios::binary|std::ios::ate);
        if(!file) throw std::runtime_error("Cannot open ELF");
        auto length=file.tellg();
        if(length<64 || length>256*1024*1024) throw std::runtime_error("Unsupported ELF file size");
        bytes.resize(static_cast<size_t>(length)); file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()),length);
        if(!file) throw std::runtime_error("Cannot read ELF");
        h=get<Header>(0);
        if(std::memcmp(h.ident,"\177ELF",4) || h.ident[4]!=2 || h.ident[5]!=1 || h.ident[6]!=1 || h.version!=1 || h.machine!=62)
            throw std::runtime_error("Expected little-endian x86_64 ELF64");
        if(h.size!=sizeof(Header) || h.phsize!=sizeof(Program) || !h.phnum || h.phnum>128 || !within(h.phoff,uint64_t(h.phnum)*sizeof(Program)))
            throw std::runtime_error("Invalid ELF program headers");
        for(unsigned i=0;i<h.phnum;i++) {
            auto p=get<Program>(h.phoff+i*sizeof(Program)); programs.push_back(p);
            if(p.type==1 && (!within(p.offset,p.filesize) || p.filesize>p.memsize || p.address>UINT64_MAX-p.memsize))
                throw std::runtime_error("Invalid ELF load segment");
            if(p.type==3) {
                if(!p.filesize || !within(p.offset,p.filesize) || bytes[p.offset+p.filesize-1]) throw std::runtime_error("Invalid ELF interpreter");
                interpreter=reinterpret_cast<const char*>(bytes.data()+p.offset);
            }
        }
        if(h.shnum && h.shnum<8192 && h.shsize==sizeof(Section) && h.shstr<h.shnum && within(h.shoff,uint64_t(h.shnum)*sizeof(Section))) {
            for(unsigned i=0;i<h.shnum;i++) sections.push_back(get<Section>(h.shoff+i*sizeof(Section)));
            const auto& names=sections[h.shstr];
            if(!within(names.offset,names.size)) throw std::runtime_error("Invalid section strings");
            for(const auto& s:sections) {
                if(s.name>=names.size) throw std::runtime_error("Invalid section name");
                const char* name=reinterpret_cast<const char*>(bytes.data()+names.offset+s.name);
                if(!std::memchr(name,0,static_cast<size_t>(names.size-s.name))) throw std::runtime_error("Unterminated section name");
                if(std::strcmp(name,".axrb.native")==0) {
                    constexpr char marker[]="AXRB_NATIVE_PROBE_V1_NO_RED_ZONE";
                    approved=s.size==sizeof(marker) && within(s.offset,s.size) && !std::memcmp(bytes.data()+s.offset,marker,sizeof(marker));
                }
            }
        }
        auto metadata=std::filesystem::path(filename);metadata+=L".axrb-code";
        if(std::filesystem::exists(metadata)) {
            std::ifstream input(metadata);std::string version,expected;
            input>>version>>expected;
            BCRYPT_ALG_HANDLE algorithm=nullptr;uint8_t digest[32];
            if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 initialization failed");
            auto status=BCryptHash(algorithm,nullptr,0,bytes.data(),static_cast<ULONG>(bytes.size()),digest,sizeof digest);
            BCryptCloseAlgorithmProvider(algorithm,0);
            std::string hash;for(auto v:digest) { hash+="0123456789abcdef"[v>>4];hash+="0123456789abcdef"[v&15]; }
            if(status<0 || version!="AXRB_X64_CODE_V1" || expected!=hash) throw std::runtime_error("Stale or invalid ELF code-range metadata");
            uint64_t begin,end,last=0;
            while(input>>std::ws && input.peek()!=std::char_traits<char>::eof()) {
                if(!(input>>std::hex>>begin>>end)) throw std::runtime_error("Truncated instruction metadata");
                if(end<=begin || begin<last) throw std::runtime_error("Invalid instruction range order");
                bool found=false;
                for(auto s:sections) if((s.flags&6)==6 && begin>=s.address && begin-s.address<s.size && end-begin<=s.size-(begin-s.address)) {
                    s.offset+=begin-s.address;s.address=begin;s.size=end-begin;code_sections.push_back(s);found=true;break;
                }
                if(!found) throw std::runtime_error("Instruction metadata outside executable section");
                last=end;
            }
            if(code_sections.empty() || !input.eof()) throw std::runtime_error("Invalid instruction metadata");
        } else code_sections=sections;
    }
    std::vector<CodePatch> decoded_gates() const {
        std::vector<CodePatch> result;
        if(sections.empty()) throw std::runtime_error("Sectionless executable instrumentation is not implemented");
        // Decode instruction boundaries, not a raw search for 0F 05 bytes.
        // Only compiler-produced SHF_EXECINSTR sections are admitted for probes.
        ZydisDecoder decoder; ZydisDecoderInit(&decoder,ZYDIS_MACHINE_MODE_LONG_64,ZYDIS_STACK_WIDTH_64);
        for(const auto& s:code_sections) if((s.flags&6)==6 && s.size) {
            bool covered=false;
            for(const auto& p:programs) if(p.type==1 && (p.flags&1) && s.address>=p.address &&
                s.address-p.address<=p.filesize && s.size<=p.filesize-(s.address-p.address) && s.offset==p.offset+s.address-p.address) covered=true;
            if(!covered || !within(s.offset,s.size)) throw std::runtime_error("Executable section outside ELF load segment");
            bool terminal=false;
            for(uint64_t at=0;at<s.size;) {
                // ART's assembly interpreter aligns its dispatch entries with
                // zero-filled padding after unconditional jumps. A linear
                // decoder otherwise eats the next entry's first opcode byte.
                if(terminal && bytes[s.offset+at]==0) {
                    auto next=at;while(next<s.size && bytes[s.offset+next]==0) ++next;
                    if(next==s.size) break;
                    if((s.address+next)%16==0) { at=next;terminal=false; }
                }
                ZydisDecodedInstruction insn{};
                if(!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder,nullptr,bytes.data()+s.offset+at,static_cast<size_t>(s.size-at),&insn)))
                    throw std::runtime_error("Cannot decode executable section at ELF address "+std::to_string(s.address+at));
                if(insn.mnemonic==ZYDIS_MNEMONIC_SYSCALL) {
                    if(insn.length!=2) throw std::runtime_error("Prefixed syscall is unsupported");
                    result.push_back({s.offset+at,s.address+at,false,{}});
                } else if(insn.mnemonic==ZYDIS_MNEMONIC_SYSENTER || insn.mnemonic==ZYDIS_MNEMONIC_INT)
                    throw std::runtime_error("Unsupported kernel entry instruction");
                else if(insn.mnemonic==ZYDIS_MNEMONIC_WRGSBASE || insn.mnemonic==ZYDIS_MNEMONIC_WRFSBASE)
                    throw std::runtime_error("Direct guest segment-base writes are unsupported");
                else if(insn.attributes&ZYDIS_ATTRIB_HAS_SEGMENT_GS) {
                    if(insn.length<2) throw std::runtime_error("Invalid GS instruction");
                    auto first=bytes.begin()+static_cast<ptrdiff_t>(s.offset+at);
                    result.push_back({s.offset+at,s.address+at,true,std::vector<uint8_t>(first,first+insn.length)});
                }
                terminal=insn.mnemonic==ZYDIS_MNEMONIC_JMP || insn.mnemonic==ZYDIS_MNEMONIC_RET || insn.mnemonic==ZYDIS_MNEMONIC_UD2 || insn.mnemonic==ZYDIS_MNEMONIC_INT3;
                at+=insn.length;
            }
        }
        return result;
    }
    void load(bool linker=false) {
        if(!approved && !linker) throw std::runtime_error("Execution requires an audited AXRB native probe; arbitrary Android/ART execution is not enabled");
        if(h.type!=3 || !interpreter.empty()) throw std::runtime_error("This milestone supports interpreter-free PIE probes only");
        for(const auto& s:sections) if(!linker && (s.flags&2) && (s.type==4 || s.type==9) && s.size)
            throw std::runtime_error("Dynamic relocations are not implemented in this milestone");
        bool executable_entry=false;
        for(const auto& p:programs) if(p.type==1) {
            span=std::max(span,up(p.address+p.memsize));
            if((p.flags&3)==3) throw std::runtime_error("Writable executable ELF segment rejected");
            if((p.flags&1) && h.entry>=p.address && h.entry-p.address<p.filesize) executable_entry=true;
        }
        if(!executable_entry || !span || span>64*1024*1024) throw std::runtime_error("Invalid probe address range or entry point");
        memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,static_cast<SIZE_T>(span),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!memory) throw std::runtime_error("Cannot allocate native ELF image");
        for(const auto& p:programs) if(p.type==1 && p.memsize) {
            auto start=reinterpret_cast<uintptr_t>(memory)+p.address;
            for(auto& r:ranges) if(start<r.end && start+p.memsize>r.begin) throw std::runtime_error("Overlapping ELF segments");
            std::memcpy(memory+p.address,bytes.data()+p.offset,static_cast<size_t>(p.filesize));
            ranges.push_back({start&~uintptr_t(page-1),reinterpret_cast<uintptr_t>(memory)+up(p.address+p.memsize),((p.flags&4)?1u:0u)|((p.flags&2)?2u:0u)|((p.flags&1)?4u:0u)});
        }
        native_tls::Batch bridges(reinterpret_cast<uintptr_t>(memory));
        for(const auto& patch:decoded_gates()) {
            auto* gate=memory+patch.address;gate[0]=0x0f;gate[1]=0x0b;
            if(patch.gs) { if(!patch_gs(gate,patch,bridges)) gs_gates.emplace(reinterpret_cast<uintptr_t>(gate),patch.instruction); }
            else gates.insert(reinterpret_cast<uintptr_t>(gate));
        }
        bridges.seal();
        DWORD old;
        if(!VirtualProtect(memory,static_cast<SIZE_T>(span),PAGE_NOACCESS,&old)) throw std::runtime_error("ELF reservation protection failed");
        for(const auto& p:programs) if(p.type==1 && p.memsize) {
            DWORD protection=(p.flags&1)?PAGE_EXECUTE_READ:(p.flags&2)?PAGE_READWRITE:(p.flags&4)?PAGE_READONLY:PAGE_NOACCESS;
            uint64_t begin=p.address&~(page-1);
            if(!VirtualProtect(memory+begin,static_cast<SIZE_T>(up(p.address+p.memsize)-begin),protection,&old)) throw std::runtime_error("ELF protection failed");
        }
        FlushInstructionCache(GetCurrentProcess(),memory,static_cast<SIZE_T>(span));
    }
    ~Image() { if(memory) VirtualFree(memory,0,MEM_RELEASE); }
};

Image* image=nullptr;
bool linker_mode=false;
bool trace_syscalls=false;
std::atomic<uint64_t> last_main_syscall{},last_main_guest_pc{},main_syscall_count{};
std::atomic<uint64_t> last_main_a{},last_main_b{},last_main_d{};
ReadOnlyFiles files;
std::string guest_program;
std::vector<std::string> program_args;
std::vector<Range> anonymous;
std::vector<Range> zero_discardable;
struct DiscardRecord { uint64_t begin,end,epoch; };
std::vector<DiscardRecord> recent_discards;
uint64_t discard_epoch=0;
struct EventCounter {uint64_t value;uint32_t flags;};
struct EpollWatch {uint32_t events;uint64_t data;};
std::unordered_map<uint64_t,EventCounter> event_counters;
std::unordered_map<uint64_t,std::unordered_map<uint64_t,EpollWatch>> epoll_sets;
uint64_t next_poll_fd=0x72000000;
std::unordered_map<DWORD,int> thread_nice;
std::unordered_map<uint64_t,Range> guest_stacks;
struct FileMapping {uint64_t begin,end,offset;std::string path;};
std::vector<FileMapping> mapped_files;
std::vector<Range> executable_ranges;
struct SharedView { uintptr_t begin,end;void* base;HANDLE section; };
std::vector<SharedView> shared_views;
std::unordered_set<uintptr_t> allocations;
thread_local uint64_t clear_tid=0;
std::unordered_set<uint64_t> unknown_syscalls;
constexpr uint64_t log_fd=0x70000000;
constexpr uint64_t binder_fd_base=0x71000000;
std::unordered_set<uint64_t> binder_fds;
std::vector<Range> binder_views;
bool log_connected=false;
thread_local uintptr_t stack_begin=0,stack_end=0;
thread_local uintptr_t guest_initial_sp=0;
thread_local uintptr_t stack_reservation=0;
thread_local uint64_t guest_tid=0;
DWORD main_host_tid=0;
thread_local bool cloned_thread=false;
thread_local CONTEXT host_exit_context{};
thread_local uintptr_t host_stack_begin=0,host_stack_end=0;
thread_local std::vector<Range> deferred_stack_unmaps;
std::recursive_mutex kernel_mutex;
thread_local std::unique_lock<std::recursive_mutex>* active_lock=nullptr;
std::unordered_set<uint64_t> guest_tids;
std::vector<HANDLE> clone_handles;
uintptr_t low_arena=0;
uint64_t low_arena_size=0;
thread_local uint64_t original_fs=0,guest_fs=0;
thread_local uint8_t* return_stub=nullptr;
thread_local uint64_t fs_recoveries=0;
thread_local uint64_t signal_mask=0;
struct SignalAction { uint64_t handler,flags,restorer,mask; };
SignalAction signal_actions[65]{};
struct SignalInbox {
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    uint64_t pending=0,waiting=0,mask=0;
    ~SignalInbox() { if(event) CloseHandle(event); }
};
std::unordered_map<uint64_t,std::shared_ptr<SignalInbox>> signal_inboxes;
struct SignalStack { uint64_t sp;uint32_t flags,pad;uint64_t size; };
thread_local SignalStack signal_stack={0,2,0,0};
void replace_range(std::vector<Range>& ranges,uintptr_t begin,uintptr_t end,uint32_t prot,bool remove=false) {
    std::vector<Range> result;
    for(const auto& r:ranges) {
        if(r.end<=begin || r.begin>=end) { result.push_back(r);continue; }
        if(r.begin<begin) result.push_back({r.begin,begin,r.prot});
        if(!remove) result.push_back({std::max(r.begin,begin),std::min(r.end,end),prot});
        if(r.end>end) result.push_back({end,r.end,r.prot});
    }
    ranges.swap(result);
}
bool covered(const std::vector<Range>& ranges,uint64_t begin,uint64_t end,uint32_t prot=0) {
    while(begin<end) {
        uint64_t next=begin;
        for(const auto& r:ranges) if(r.begin<=begin && r.end>begin && (r.prot&prot)==prot) next=std::max<uint64_t>(next,r.end);
        if(next==begin) return false;
        begin=next;
    }
    return true;
}
bool access(uint64_t address,uint64_t length,bool write=false) {
    if(!length) return true;
    if(address>UINTPTR_MAX-length) return false;
    auto end=address+length;
    if(address>=stack_begin && end<=stack_end) return true;
    for(const auto& [tid,range]:guest_stacks) if(address>=range.begin && end<=range.end) return true;
    for(const auto* ranges:{&image->ranges,&anonymous,&binder_views}) if(covered(*ranges,address,end,write?2:1)) return true;
    return false;
}
struct Timespec { int64_t sec,ns; };
int64_t signal_wait(CONTEXT& c) {
    if(c.R10!=8) return -22;
    if(!access(c.Rdi,8) || (c.Rsi && !access(c.Rsi,128,true)) || (c.Rdx && !access(c.Rdx,sizeof(Timespec)))) return -14;
    uint64_t wanted=*reinterpret_cast<uint64_t*>(c.Rdi)&~((1ull<<8)|(1ull<<18));
    auto& slot=signal_inboxes[guest_tid];if(!slot) slot=std::make_shared<SignalInbox>();
    auto inbox=slot;if(!inbox->event) return -12;
    auto deadline=std::chrono::steady_clock::time_point::max();
    if(c.Rdx) {
        auto ts=*reinterpret_cast<Timespec*>(c.Rdx);
        if(ts.sec<0 || ts.ns<0 || ts.ns>=1000000000) return -22;
        if(ts.sec<315360000) deadline=std::chrono::steady_clock::now()+std::chrono::seconds(ts.sec)+std::chrono::nanoseconds(ts.ns);
    }
    for(;;) {
        uint64_t ready=inbox->pending&wanted;
        if(ready) {
            unsigned long bit=0;_BitScanForward64(&bit,ready);inbox->pending&=~(1ull<<bit);inbox->waiting=0;
            if(c.Rsi) {
                auto* info=reinterpret_cast<int32_t*>(c.Rsi);std::memset(info,0,128);
                info[0]=bit+1;info[2]=-6;info[4]=GetCurrentProcessId();info[5]=10000;
            }
            return bit+1;
        }
        if(std::chrono::steady_clock::now()>=deadline) { inbox->waiting=0;return -11; }
        DWORD milliseconds=INFINITE;
        if(deadline!=std::chrono::steady_clock::time_point::max()) {
            auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            milliseconds=static_cast<DWORD>(std::clamp<int64_t>(left+1,0,INFINITE-1));
        }
        inbox->waiting=wanted;ResetEvent(inbox->event);
        active_lock->unlock();DWORD result=WaitForSingleObject(inbox->event,milliseconds);active_lock->lock();
        if(result==WAIT_FAILED) {inbox->waiting=0;return -4;}
    }
}
int64_t signal_send(uint64_t tid,uint64_t number) {
    if(number>64) return -22;
    if(!guest_tids.count(tid)) return -3;
    if(!number) return 0;
    auto& inbox=signal_inboxes[tid];if(!inbox) inbox=std::make_shared<SignalInbox>();
    uint64_t bit=1ull<<(number-1);
    if(!((inbox->mask|inbox->waiting)&bit)) {
        if(signal_actions[number].handler==1) return 0;
        // Asynchronous handler delivery needs a Linux signal frame; never pretend it ran.
        return -38;
    }
    inbox->pending|=bit;SetEvent(inbox->event);return 0;
}
struct FutexWaiter {
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    uint32_t bits=~0u;bool woken=false;uint64_t address=0;
    ~FutexWaiter() { if(event) CloseHandle(event); }
};
std::unordered_map<uint64_t,std::vector<std::shared_ptr<FutexWaiter>>> futex_waiters;
int futex_wake(uint64_t address,uint32_t count,uint32_t bits=~0u) {
    auto it=futex_waiters.find(address);if(it==futex_waiters.end()) return 0;
    int woken=0;
    for(auto& waiter:it->second) if(!waiter->woken && (waiter->bits&bits) && static_cast<uint32_t>(woken)<count) {
        waiter->woken=true;SetEvent(waiter->event);++woken;
    }
    return woken;
}
int64_t futex_guest(CONTEXT& c) {
    auto address=c.Rdi;uint32_t op=static_cast<uint32_t>(c.Rsi),value=static_cast<uint32_t>(c.Rdx);
    if(address%4 || !access(address,4)) return -14;
    uint32_t command=op&0x7f;
    if(op&~0x1ffu) return -22;
    if(command==3 || command==4) {
        uint64_t target=c.R8;uint32_t requeue=static_cast<uint32_t>(c.R10);
        if(op&0x100 || target==address || target%4 || !access(target,4) || value>INT32_MAX || requeue>INT32_MAX) return -22;
        if(command==4 && *reinterpret_cast<volatile uint32_t*>(address)!=static_cast<uint32_t>(c.R9)) return -11;
        int woken=futex_wake(address,value);uint32_t moved=0;
        auto it=futex_waiters.find(address);
        if(it!=futex_waiters.end()) {
            std::vector<std::shared_ptr<FutexWaiter>> remaining;
            auto source=std::move(it->second);futex_waiters.erase(it);
            for(auto& waiter:source) {
                if(!waiter->woken && moved<requeue) {waiter->address=target;futex_waiters[target].push_back(waiter);++moved;}
                else remaining.push_back(waiter);
            }
            if(!remaining.empty()) futex_waiters[address]=std::move(remaining);
        }
        return static_cast<int64_t>(woken)+moved;
    }
    if(command==1 || command==10) {
        if(op&0x100 || (command==10 && !c.R9) || value>INT32_MAX) return -22;
        return futex_wake(address,value,command==10?static_cast<uint32_t>(c.R9):~0u);
    }
    if(command!=0 && command!=9) return -38;
    if((command==0 && (op&0x100)) || (command==9 && !c.R9)) return -22;
    if(*reinterpret_cast<volatile uint32_t*>(address)!=value) return -11;
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::time_point::max();
    if(c.R10) {
        if(!access(c.R10,sizeof(Timespec))) return -14;
        auto ts=*reinterpret_cast<Timespec*>(c.R10);
        if(ts.sec<0 || ts.ns<0 || ts.ns>=1000000000) return -22;
        long double seconds=static_cast<long double>(ts.sec)+static_cast<long double>(ts.ns)/1000000000;
        if(command==9) {
            if(op&0x100) { FILETIME t;GetSystemTimePreciseAsFileTime(&t);seconds-=static_cast<long double>(((uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime)-116444736000000000ULL)/10000000; }
            else { LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);seconds-=static_cast<long double>(t.QuadPart)/f.QuadPart; }
        }
        if(seconds<=0) return -110;
        // Saturate far-future waits instead of overflowing chrono/int milliseconds.
        if(seconds<315360000) deadline=std::chrono::steady_clock::now()+std::chrono::nanoseconds(static_cast<int64_t>(seconds*1000000000));
    }
    auto waiter=std::make_shared<FutexWaiter>();if(!waiter->event) return -12;
    waiter->address=address;waiter->bits=command==9?static_cast<uint32_t>(c.R9):~0u;futex_waiters[address].push_back(waiter);
    DWORD result=WAIT_TIMEOUT;
    do {
        DWORD milliseconds=INFINITE;
        if(deadline!=std::chrono::steady_clock::time_point::max()) {
            auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            milliseconds=static_cast<DWORD>(std::clamp<int64_t>(left+1,0,INFINITE-1));
        }
        active_lock->unlock();result=WaitForSingleObject(waiter->event,milliseconds);active_lock->lock();
    } while(result==WAIT_TIMEOUT && std::chrono::steady_clock::now()<deadline && !waiter->woken);
    auto& queue=futex_waiters[waiter->address];queue.erase(std::remove(queue.begin(),queue.end(),waiter),queue.end());if(queue.empty()) futex_waiters.erase(waiter->address);
    return waiter->woken?0:result==WAIT_TIMEOUT?-110:-4;
}
struct CloneStart { CONTEXT context;uint64_t tls,gs,mask,tid_address,flags;Range stack; };
DWORD WINAPI enter_clone(void* pointer);
int64_t clone_guest(CONTEXT& c) {
    constexpr uint64_t required=0x100|0x200|0x400|0x800|0x10000;
    constexpr uint64_t allowed=required|0x40000|0x80000|0x100000|0x200000|0x1000000;
    if((c.Rdi&required)!=required || c.Rdi&~allowed || !c.Rsi || !(c.Rdi&0x80000)) return -22;
    if(!access(c.R8,16) || ((c.Rdi&0x100000) && !access(c.Rdx,4,true)) || ((c.Rdi&0x1200000) && !access(c.R10,4,true))) return -14;
    Range stack{};
    for(const auto& r:anonymous) if(c.Rsi>=r.begin+65536 && c.Rsi+16<=r.end && (r.prot&3)==3) {stack=r;break;}
    if(!stack.end) return -14;
    auto start=std::make_unique<CloneStart>(CloneStart{c,c.R8,native_tls::gs_base,signal_mask,c.R10,c.Rdi,stack});
    start->context.Rip+=2;start->context.Rsp=c.Rsi;start->context.Rax=0;start->context.Rcx=start->context.Rip;start->context.R11=c.EFlags;
    DWORD tid=0;HANDLE thread=CreateThread(nullptr,1024*1024,enter_clone,start.get(),CREATE_SUSPENDED,&tid);
    if(!thread) return -11;
    guest_tids.insert(tid);
    signal_inboxes[tid]=std::make_shared<SignalInbox>();signal_inboxes[tid]->mask=signal_mask;
    if(c.Rdi&0x100000) *reinterpret_cast<uint32_t*>(c.Rdx)=tid;
    start.release();clone_handles.push_back(thread);ResumeThread(thread);return tid;
}
[[noreturn]] void finish_guest(DWORD code);
bool guest_string(uint64_t pointer,std::string& value) {
    value.clear();
    for(size_t i=0;i<4096;i++) {
        if(!access(pointer+i,1)) return false;
        char c=*reinterpret_cast<const char*>(pointer+i);
        if(!c) return true;
        value+=c;
    }
    return false;
}
int64_t file_syscall(CONTEXT& c) {
    uint64_t a=c.Rdi,b=c.Rsi,d=c.Rdx;
    std::string path;
    switch(c.Rax) {
    case 0: case 17:
        if(!access(b,d,true)) return -14;
        return files.read(static_cast<int>(a),reinterpret_cast<void*>(b),d,c.R10,c.Rax==0);
    case 2: case 257: {
        uint64_t name=c.Rax==2?a:b,flags=c.Rax==2?b:d;
        if(!guest_string(name,path)) return -14;
        if(path.empty() || path[0]!='/') return -2; // Directory-relative paths are not implemented yet.
        if(path=="/proc/self/maps") {
            std::vector<Range> maps=image->ranges;
            maps.insert(maps.end(),anonymous.begin(),anonymous.end());
            maps.push_back({stack_begin,stack_end,3});
            std::sort(maps.begin(),maps.end(),[](auto& x,auto& y){return x.begin<y.begin;});
            std::ostringstream text;
            for(const auto& r:maps) text<<std::hex<<r.begin<<'-'<<r.end<<' '<<((r.prot&1)?'r':'-')<<((r.prot&2)?'w':'-')<<((r.prot&4)?'x':'-')<<"p 00000000 00:00 0 "<<(r.begin==stack_begin?"[stack]":"")<<'\n';
            files.text(path,text.str());
        }
        if(path=="/proc/self/stat") {
            std::ostringstream text;text<<GetCurrentProcessId()<<" (axrb-native) R";
            for(int field=4;field<=52;field++) text<<' '<<(field==28?guest_initial_sp:field==20?1:0);
            text<<'\n';files.text(path,text.str());
        }
        if(path=="/dev/binder") {
            auto fd=arm_bridge::binder(0,0,flags);if(fd<0)return fd;
            auto exposed=binder_fd_base+static_cast<uint64_t>(fd);binder_fds.insert(exposed);return exposed;
        }
        int result=files.open(path,flags);
        if(trace_syscalls) std::fprintf(stderr,"open %s => %d\n",path.c_str(),result);
        return result;
    }
    case 3: if(binder_fds.erase(a))return arm_bridge::binder(3,a-binder_fd_base);return files.close(static_cast<int>(a));
    case 8: {
        auto* file=files.get(static_cast<int>(a));if(!file) return -9;
        int64_t offset=static_cast<int64_t>(b),base=0;
        if(d==1) base=static_cast<int64_t>(file->offset);
        else if(d==2) { LinuxStat info;int result=files.stat(static_cast<int>(a),info);if(result) return result;base=info.size; }
        else if(d!=0) return -22;
        if((offset>0 && base>INT64_MAX-offset) || (offset<0 && offset<-base)) return -22;
        file->offset=static_cast<uint64_t>(base+offset);return base+offset;
    }
    case 5:
        if(!access(b,sizeof(LinuxStat),true)) return -14;
        return files.stat(static_cast<int>(a),*reinterpret_cast<LinuxStat*>(b));
    case 4: case 262: {
        uint64_t name=c.Rax==4?a:b,output=c.Rax==4?b:d;
        if(c.Rax==262 && c.R10) return -22;
        if(!guest_string(name,path) || !access(output,sizeof(LinuxStat),true)) return -14;
        return files.stat(path,*reinterpret_cast<LinuxStat*>(output));
    }
    case 21: case 269: {
        uint64_t name=c.Rax==21?a:b,mode=c.Rax==21?b:d;
        if(mode&~7ull) return -22;
        if(!guest_string(name,path)) return -14;
        LinuxStat st;int result=files.stat(path,st);
        return result?result:(mode&2)&&!files.writable(path)?-30:0;
    }
    case 89: case 267: {
        uint64_t name=c.Rax==89?a:b,out=c.Rax==89?b:d,length=c.Rax==89?d:c.R10;
        if(!length) return -22;
        if(!guest_string(name,path) || !access(out,length,true)) return -14;
        std::string target;
        if(path=="/proc/self/exe") target="/apex/com.android.runtime/bin/linker64";
        else if(path.rfind("/proc/self/fd/",0)==0) {
            try { auto* f=files.get(std::stoi(path.substr(14)));if(f) target=f->path; } catch(...) {}
        } else { try { target=files.readlink(path); } catch(...) { return -2; } }
        if(target.empty()) return -22;
        auto size=std::min<uint64_t>(target.size(),length);std::memcpy(reinterpret_cast<void*>(out),target.data(),static_cast<size_t>(size));return size;
    }
    default:return -38;
    }
}
int64_t syscall(CONTEXT& c) {
    uint64_t a=c.Rdi,b=c.Rsi,d=c.Rdx;
    if(guest_tid==GetCurrentProcessId()) {last_main_syscall.store(c.Rax,std::memory_order_relaxed);last_main_guest_pc.store(c.Rip,std::memory_order_relaxed);main_syscall_count.fetch_add(1,std::memory_order_relaxed);}
    if(guest_tid==GetCurrentProcessId()){last_main_a.store(a,std::memory_order_relaxed);last_main_b.store(b,std::memory_order_relaxed);last_main_d.store(d,std::memory_order_relaxed);}
    if(trace_syscalls) std::fprintf(stderr,"syscall %llu (%llx,%llx,%llx,%llx,%llx,%llx)\n",c.Rax,a,b,d,c.R10,c.R8,c.R9);
    switch(c.Rax) {
    case 0x41585244: {
        std::string name;if(!guest_string(c.R9,name))return -14;
        uint64_t args[]={a,b,d,c.R10,c.R8};return arm_bridge::platform_host(name.c_str(),args);
    }
    case AXRB_ARM_SYSCALL: return arm_bridge::dispatch(a,b,d,c.R10,access,&files,[]{active_lock->unlock();},[]{active_lock->lock();});
    case 128: return signal_wait(c);
    case 140:case 141: {
        if(a!=0)return -22;
        DWORD tid=b?static_cast<DWORD>(b):guest_tid;
        if(tid!=GetCurrentProcessId() && !guest_tids.count(tid))return -3;
        if(c.Rax==140)return 20-thread_nice[tid];
        int nice=std::clamp(static_cast<int32_t>(d),-20,19);
        HANDLE thread=OpenThread(THREAD_SET_INFORMATION,FALSE,tid==GetCurrentProcessId()?main_host_tid:tid);if(!thread)return -3;
        int priority=nice>=10?THREAD_PRIORITY_BELOW_NORMAL:nice<0?THREAD_PRIORITY_ABOVE_NORMAL:THREAD_PRIORITY_NORMAL;
        bool ok=SetThreadPriority(thread,priority)!=0;CloseHandle(thread);
        if(!ok)return -13;thread_nice[tid]=nice;return 0;
    }
    case 24: active_lock->unlock();SwitchToThread();active_lock->lock();return 0;
    case 200: return signal_send(a,b);
    case 234: return a==GetCurrentProcessId()?signal_send(b,d):-3;
    case 13: {
        if(!a || a>64 || a==9 || a==19 || c.R10!=8) return -22;
        if((b && !access(b,sizeof(SignalAction))) || (d && !access(d,sizeof(SignalAction),true))) return -14;
        auto previous=signal_actions[a];
        if(b) signal_actions[a]=*reinterpret_cast<SignalAction*>(b);
        if(d) *reinterpret_cast<SignalAction*>(d)=previous;
        return 0;
    }
    case 131: {
        if((a && !access(a,sizeof(SignalStack))) || (b && !access(b,sizeof(SignalStack),true))) return -14;
        auto previous=signal_stack;
        if(a) {
            auto next=*reinterpret_cast<SignalStack*>(a);
            if(next.flags&~2u) return -22;
            if(!(next.flags&2) && (next.size<2048 || !access(next.sp,next.size,true))) return -12;
            signal_stack=next;
        }
        if(b) *reinterpret_cast<SignalStack*>(b)=previous;return 0;
    }
    case 14: { // rt_sigprocmask: Linux kernel sigset_t is 64 bits on x86_64.
        if(c.R10!=8 || (a>2 && b)) return -22;
        if((b && !access(b,8)) || (d && !access(d,8,true))) return -14;
        uint64_t previous=signal_mask;
        if(b) {
            uint64_t value=*reinterpret_cast<uint64_t*>(b);
            if(a==0) signal_mask|=value;else if(a==1) signal_mask&=~value;else signal_mask=value;
            signal_mask&=~((1ull<<8)|(1ull<<18)); // SIGKILL/SIGSTOP cannot be blocked.
            auto& inbox=signal_inboxes[guest_tid];if(!inbox) inbox=std::make_shared<SignalInbox>();inbox->mask=signal_mask;
        }
        if(d) *reinterpret_cast<uint64_t*>(d)=previous;
        return 0;
    }
    case 41:
        if(a==1 && (b&0xf)==2 && d==0) return log_fd;
        return -97;
    case 42: {
        if(a!=log_fd) return -9;
        if(d<3 || d>110 || !access(b,d)) return -14;
        if(*reinterpret_cast<uint16_t*>(b)!=1) return -97;
        const char* path=reinterpret_cast<char*>(b+2);
        if(!std::memchr(path,0,static_cast<size_t>(d-2))) return -22;
        if(std::strcmp(path,"/dev/socket/logdw")) return -2;
        log_connected=true;return 0;
    }
    case 46: {
        if(a!=log_fd || !log_connected) return -9;
        if(!access(b,56)) return -14;
        auto* header=reinterpret_cast<uint64_t*>(b);
        uint64_t vectors=header[2],count=header[3];
        if(count>64 || !access(vectors,count*16)) return -14;
        int64_t total=0;
        for(uint64_t i=0;i<count;i++) {
            auto* v=reinterpret_cast<uint64_t*>(vectors+i*16);
            if(v[1]>65536 || !access(v[0],v[1])) return -14;
            if(i>=2 && v[1]) {
                auto* text=reinterpret_cast<char*>(v[0]);size_t length=static_cast<size_t>(v[1]);
                if(text[length-1]==0) --length;
                std::fwrite(text,1,length,stderr);if(i==2) std::fputs(": ",stderr);
            }
            total+=v[1];
        }
        std::fputc('\n',stderr);return total;
    }
    case 0:
        if(auto it=event_counters.find(a);it!=event_counters.end()) {
            if(d<8)return -22;if(!access(b,8,true))return -14;
            if(!it->second.value)return -11;
            *reinterpret_cast<uint64_t*>(b)=(it->second.flags&1)?1:it->second.value;
            it->second.value-=(it->second.flags&1)?1:it->second.value;return 8;
        }
        return file_syscall(c);
    case 3:
        if(event_counters.erase(a)||epoll_sets.erase(a))return 0;
        return file_syscall(c);
    case 290:
        if(b&~0x80801ull)return -22;
        event_counters.emplace(next_poll_fd,EventCounter{static_cast<uint32_t>(a),static_cast<uint32_t>(b)});return next_poll_fd++;
    case 291:
        if(a&~0x80000ull)return -22;
        epoll_sets.emplace(next_poll_fd,std::unordered_map<uint64_t,EpollWatch>{});return next_poll_fd++;
    case 233: {
        auto set=epoll_sets.find(a);if(set==epoll_sets.end())return -9;
        if(!event_counters.count(d))return -9;
        auto& watches=set->second;
        if(b==2)return watches.erase(d)?0:-2;
        if(b!=1 && b!=3)return -22;
        if(!access(c.R10,12))return -14;
        if(b==1 && watches.count(d))return -17;
        if(b==3 && !watches.count(d))return -2;
        EpollWatch watch{};std::memcpy(&watch.events,reinterpret_cast<void*>(c.R10),4);
        std::memcpy(&watch.data,reinterpret_cast<void*>(c.R10+4),8);watches[d]=watch;return 0;
    }
    case 7:case 271: {
        if(b>65536)return -22;if(b && !access(a,b*8,true))return -14;
        int64_t timeout=static_cast<int32_t>(d);
        if(c.Rax==271) {
            if(c.R10)return -38;
            if(!d)timeout=-1;
            else {if(!access(d,16))return -14;auto ts=*reinterpret_cast<Timespec*>(d);
                if(ts.sec<0||ts.sec>86400||ts.ns<0||ts.ns>=1000000000)return -22;
                timeout=ts.sec*1000+(ts.ns+999999)/1000000;}
        }
        auto started=GetTickCount64();
        for(;;) {
            int64_t ready=0;
            for(uint64_t i=0;i<b;i++) {
                auto p=reinterpret_cast<uint8_t*>(a+i*8);int32_t fd;uint16_t events,revents=0;
                std::memcpy(&fd,p,4);std::memcpy(&events,p+4,2);
                if(fd>=0) {
                    auto event=event_counters.find(fd);
                    if(event!=event_counters.end())revents=events&((event->second.value?1:0)|(event->second.value<UINT64_MAX-1?4:0));
                    else if(files.get(fd))revents=events&5;
                    else revents=32;
                }
                std::memcpy(p+6,&revents,2);if(revents)++ready;
            }
            if(ready || (timeout>=0 && GetTickCount64()-started>=static_cast<uint64_t>(timeout)))return ready;
            active_lock->unlock();Sleep(1);active_lock->lock();
        }
    }
    case 232:case 281: {
        if(!epoll_sets.count(a))return -9;
        if(!d || d>65536)return -22;
        if(!access(b,d*12,true))return -14;
        if(c.Rax==281 && c.R8)return -38;
        auto timeout=static_cast<int32_t>(c.R10);auto started=GetTickCount64();
        for(;;) {
            auto set=epoll_sets.find(a);if(set==epoll_sets.end())return -9;
            int64_t count=0;
            for(auto& entry:set->second) {
                auto event=event_counters.find(entry.first);if(event==event_counters.end())continue;
                uint32_t ready=(event->second.value?1:0)|(event->second.value<UINT64_MAX-1?4:0);
                ready&=entry.second.events;
                if(!ready)continue;
                std::memcpy(reinterpret_cast<void*>(b+count*12),&ready,4);
                std::memcpy(reinterpret_cast<void*>(b+count*12+4),&entry.second.data,8);
                if(entry.second.events&(1u<<30))entry.second.events=0;
                if(++count==static_cast<int64_t>(d))break;
            }
            if(count || (timeout>=0 && GetTickCount64()-started>=static_cast<uint64_t>(timeout)))return count;
            active_lock->unlock();Sleep(1);active_lock->lock();
        }
    }
    case 2:case 4:case 5:case 8:case 17:case 21:case 89:case 257:case 262:case 267:case 269:
        return file_syscall(c);
    case 20: { // writev, Linux x86_64 iovec is two 64-bit words.
        if(a==log_fd) {
            if(!log_connected)return -107;
            if(d>64 || !access(b,d*16))return -14;
            int64_t total=0;
            for(uint64_t i=0;i<d;i++) {
                auto* v=reinterpret_cast<uint64_t*>(b)+2*i;
                if(v[1]>65536 || !access(v[0],v[1]))return -14;
                total+=v[1];
                if(i+2>=d && v[1]) {
                    auto* message=reinterpret_cast<const char*>(v[0]);size_t length=static_cast<size_t>(v[1]);
                    if(!message[length-1])--length;
                    std::fwrite(message,1,length,stderr);if(i+2==d)std::fputs(": ",stderr);
                }
            }
            std::fputc('\n',stderr);return total;
        }
        if(a!=1 && a!=2) return -9;
        if(d>1024 || !access(b,d*16)) return -14;
        int64_t total=0;
        for(uint64_t i=0;i<d;i++) {
            auto* v=reinterpret_cast<uint64_t*>(b)+2*i;
            if(!access(v[0],v[1]) || v[1]>MAXDWORD) return total?total:-14;
            DWORD written=0;
            if(!WriteFile(GetStdHandle(a==1?STD_OUTPUT_HANDLE:STD_ERROR_HANDLE),reinterpret_cast<void*>(v[0]),static_cast<DWORD>(v[1]),&written,nullptr)) return total?total:-5;
            total+=written;
            if(written!=v[1]) break;
        }
        return total;
    }
    case 1: {
        if(auto it=event_counters.find(a);it!=event_counters.end()) {
            if(d!=8)return -22;if(!access(b,8))return -14;
            auto value=*reinterpret_cast<const uint64_t*>(b);if(value==UINT64_MAX)return -22;
            if(value>UINT64_MAX-1-it->second.value)return -11;
            it->second.value+=value;return 8;
        }
        if(a!=1 && a!=2) {if(!access(b,d))return -14;return files.write(static_cast<int>(a),reinterpret_cast<void*>(b),d);}
        if(!access(b,d) || d>MAXDWORD) return -14;
        DWORD written=0;
        return WriteFile(GetStdHandle(a==1?STD_OUTPUT_HANDLE:STD_ERROR_HANDLE),reinterpret_cast<void*>(b),static_cast<DWORD>(d),&written,nullptr)?static_cast<int64_t>(written):-5;
    }
    case 83:case 258: {
        std::string path;if(!guest_string(c.Rax==83?a:b,path))return -14;
        if(path.empty()||path[0]!='/')return -38;
        return files.mkdir(path);
    }
    case 82:case 264:case 316: {
        std::string from,to;
        if(!guest_string(c.Rax==82?a:b,from)||!guest_string(c.Rax==82?b:c.R10,to))return -14;
        if(c.Rax==316 && c.R8&~1ull)return -22;
        return files.rename(from,to,c.Rax==316 && (c.R8&1));
    }
    case 84:case 87:case 263: {
        std::string path;if(!guest_string(c.Rax==263?b:a,path))return -14;
        if(c.Rax==263 && d&~0x200ull)return -22;
        return files.unlink(path,c.Rax==84 || (c.Rax==263 && (d&0x200)));
    }
    case 74:case 75:return files.sync(static_cast<int>(a));
    case 77:return files.truncate(static_cast<int>(a),b);
    case 217:
        if(!access(b,d,true))return -14;
        return files.getdents(static_cast<int>(a),reinterpret_cast<void*>(b),d);
    case 16: {
        if(!binder_fds.count(a))return -25;
        // Binder read transactions can block while another x86 thread replies.
        active_lock->unlock();auto result=arm_bridge::binder(1,a-binder_fd_base,b,d);active_lock->lock();return result;
    }
    case 9: {
        if(binder_fds.count(c.R8)) {
            if(a || b>4*1024*1024 || !b || (d&~1ull) || c.R10&0x10)return -22;
            auto mapped=arm_bridge::binder(2,c.R8-binder_fd_base,b,d,c.R10,c.R9);
            if(mapped>=0)binder_views.push_back({static_cast<uint64_t>(mapped),static_cast<uint64_t>(mapped)+up(b),static_cast<uint32_t>(d)});
            return mapped;
        }
        const uint64_t flags=c.R10,offset=c.R9,length=b?up(b):0;
        const bool fixed=(flags&0x10)!=0,anon=(flags&0x20)!=0;
        const bool noreplace=(flags&0x100000)!=0;
        if(!b || b>(1ull<<40) || d&~7ull || (d&6)==6 || ((flags&3)!=2 && (flags&3)!=1) || (flags&~0x106873ull) || offset%page) return -22;
        if(anon && (static_cast<int32_t>(c.R8)!=-1 || offset || (d&4))) return -13;
        NativeFile* file=anon?nullptr:files.get(static_cast<int>(c.R8));
        if(!anon && !file) return -9;
        if((flags&3)==1) {
            // Real Windows section views, not private snapshots. Writable/JIT
            // aliases and fixed/partial view replacement require a later stage.
            if(anon || fixed || noreplace || !file || file->handle==INVALID_HANDLE_VALUE || (d&6)) return -13;
            LinuxStat info;if(files.stat(static_cast<int>(c.R8),info)) return -9;
            if(offset>=static_cast<uint64_t>(info.size) || b>up(static_cast<uint64_t>(info.size))-offset) return -22;
            uint64_t aligned=offset&~uint64_t(65535),delta=offset-aligned;
            HANDLE section=CreateFileMappingW(file->handle,nullptr,PAGE_READONLY,0,0,nullptr);
            if(!section) return -12;
            auto* view=static_cast<uint8_t*>(MapViewOfFile(section,FILE_MAP_READ,static_cast<DWORD>(aligned>>32),static_cast<DWORD>(aligned),static_cast<SIZE_T>(std::min<uint64_t>(b+delta,info.size-aligned))));
            if(!view) { CloseHandle(section);return -12; }
            auto start=reinterpret_cast<uintptr_t>(view+delta);
            if(!d) { DWORD old;if(!VirtualProtect(reinterpret_cast<void*>(start),static_cast<SIZE_T>(length),PAGE_NOACCESS,&old)) {UnmapViewOfFile(view);CloseHandle(section);return -12;} }
            shared_views.push_back({start,start+length,view,section});anonymous.push_back({start,start+length,static_cast<uint32_t>(d)});
            return start;
        }
        if(fixed && (a%page || a>UINT64_MAX-length || !covered(anonymous,a,a+length))) return -12;
        const bool reserve_only=anon && d==0;
        if(noreplace && (!a || a%page || a>UINT64_MAX-length)) return -22;
        void* reservation=nullptr;
        void* allocation=fixed?reinterpret_cast<void*>(a):nullptr;
        if(!fixed) {
            if(a>=low_arena && low_arena && a<low_arena+low_arena_size && length<=low_arena+low_arena_size-a) {
                bool free=true;for(const auto& r:anonymous) if(a<r.end && a+length>r.begin) free=false;
                if(free) {
                    allocation=reserve_only?reinterpret_cast<void*>(a):VirtualAlloc(reinterpret_cast<void*>(a),static_cast<SIZE_T>(length),MEM_COMMIT,PAGE_READWRITE);
                    reservation=reinterpret_cast<void*>(low_arena);
                }
            }
            if(a && !allocation) {
                auto base=a&~uint64_t(65535);
                reservation=VirtualAlloc(reinterpret_cast<void*>(base),static_cast<SIZE_T>(length+a-base),reserve_only?MEM_RESERVE:MEM_RESERVE|MEM_COMMIT,reserve_only?PAGE_NOACCESS:PAGE_READWRITE);
                if(reservation) allocation=reinterpret_cast<void*>(a&~(page-1));
            }
            if(!allocation && noreplace) return -17;
            // A Linux hint is not a fixed-address request. ART rejects high
            // mappings for compressed references, then probes every page in a
            // huge candidate range. Find another low hole instead of returning
            // an arbitrary high Windows allocation when its hint is occupied.
            bool wants_low=(a && a<0x100000000ull) || (flags&0x40);
            if(!allocation && low_arena && wants_low && length<=low_arena_size) {
                auto maps=anonymous;
                std::sort(maps.begin(),maps.end(),[](const auto& x,const auto& y){return x.begin<y.begin;});
                uint64_t candidate=low_arena;
                for(const auto& range:maps) {
                    if(range.end<=candidate)continue;
                    if(range.begin>=candidate && range.begin-candidate>=length)break;
                    if(range.begin<candidate+length)candidate=up(range.end);
                    if(candidate>low_arena+low_arena_size-length)break;
                }
                if(candidate<=low_arena+low_arena_size-length) {
                    allocation=reserve_only?reinterpret_cast<void*>(candidate):VirtualAlloc(reinterpret_cast<void*>(candidate),static_cast<SIZE_T>(length),MEM_COMMIT,PAGE_READWRITE);
                    if(allocation)reservation=reinterpret_cast<void*>(low_arena);
                }
            }
            if(!allocation && wants_low) {
                // Compressed ART references can address all of low 4 GiB. The
                // initial arena may be too small for framework boot allocations.
                for(uintptr_t candidate=0x10000;candidate<0x100000000ull;) {
                    MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(reinterpret_cast<void*>(candidate),&info,sizeof info))break;
                    auto end=std::min<uint64_t>(0x100000000ull,reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize);
                    auto begin=(candidate+65535)&~uint64_t(65535);
                    if(info.State==MEM_FREE && begin<end && length<=end-begin) {
                        reservation=VirtualAlloc(reinterpret_cast<void*>(begin),static_cast<SIZE_T>(length),reserve_only?MEM_RESERVE:MEM_RESERVE|MEM_COMMIT,reserve_only?PAGE_NOACCESS:PAGE_READWRITE);
                        if(reservation){allocation=reservation;break;}
                    }
                    if(end<=candidate)break;candidate=end;
                }
            }
            if(!allocation && wants_low)return -12;
            if(!allocation) allocation=reservation=VirtualAlloc(nullptr,static_cast<SIZE_T>(length),reserve_only?MEM_RESERVE:MEM_RESERVE|MEM_COMMIT,reserve_only?PAGE_NOACCESS:PAGE_READWRITE);
        }
        if(!allocation) return -12;
        auto start=reinterpret_cast<uintptr_t>(allocation);
        DWORD previous;
        if(fixed && !reserve_only && (!VirtualAlloc(allocation,static_cast<SIZE_T>(length),MEM_COMMIT,PAGE_READWRITE) || !VirtualProtect(allocation,static_cast<SIZE_T>(length),PAGE_READWRITE,&previous))) return -12;
        if(!reserve_only) std::memset(allocation,0,static_cast<size_t>(length));
        if(!fixed) { allocations.insert(reinterpret_cast<uintptr_t>(reservation));anonymous.push_back({start,start+length,3}); }
        else replace_range(anonymous,start,start+length,3);
        replace_range(executable_ranges,start,start+length,0,true);
        replace_range(zero_discardable,start,start+length,0,true);
        if(anon) zero_discardable.push_back({start,start+length,0});
        for(auto it=image->gates.begin();it!=image->gates.end();) {
            if(*it>=start && *it<start+length) it=image->gates.erase(it);else ++it;
        }
        for(auto it=image->gs_gates.begin();it!=image->gs_gates.end();) {
            if(it->first>=start && it->first<start+length) it=image->gs_gates.erase(it);else ++it;
        }
        int64_t result=0;
        if(file) result=files.read(static_cast<int>(c.R8),allocation,b,offset,false);
        if(result>=0 && (d&4)) {
            try {
                Image source(files.host(file->path).c_str());
                bool valid=false;
                for(const auto& segment:source.programs) if(segment.type==1 && (segment.flags&1) && offset>=(segment.offset&~(page-1)) && offset<up(segment.offset+segment.filesize) && b<=up(segment.offset+segment.filesize)-offset) valid=true;
                if(!valid) throw std::runtime_error("Executable mapping is not an ELF code segment");
                native_tls::Batch bridges(start);
                for(const auto& patch:source.decoded_gates()) {
                    auto at=patch.offset;
                    if(at>=offset && at-offset<length) {
                        if(std::max<size_t>(2,patch.instruction.size())>length-(at-offset)) throw std::runtime_error("Instruction crosses executable mapping boundary");
                        auto* gate=static_cast<uint8_t*>(allocation)+at-offset;gate[0]=0x0f;gate[1]=0x0b;
                        if(patch.gs) { if(!patch_gs(gate,patch,bridges)) image->gs_gates[reinterpret_cast<uintptr_t>(gate)]=patch.instruction; }
                        else image->gates.insert(reinterpret_cast<uintptr_t>(gate));
                    }
                }
                bridges.seal();
                executable_ranges.push_back({start,start+length,4});
            } catch(const std::exception& error) { std::fprintf(stderr,"native-x64 executable mapping %s: %s\n",file->path.c_str(),error.what());result=-8; }
        }
        if(file && result>=0)mapped_files.push_back({start,start+length,offset,file->path});
        DWORD protection=result<0?PAGE_NOACCESS:(d&4)?PAGE_EXECUTE_READ:(d&2)?PAGE_READWRITE:(d&1)?PAGE_READONLY:PAGE_NOACCESS;
        if(!reserve_only && !VirtualProtect(allocation,static_cast<SIZE_T>(length),protection,&previous)) result=-12;
        replace_range(anonymous,start,start+length,result<0?0:static_cast<uint32_t>(d));
        FlushInstructionCache(GetCurrentProcess(),allocation,static_cast<SIZE_T>(length));
        return result<0?result:static_cast<int64_t>(start);
    }
    case 10: case 11: {
        if(a%page || !b || b>UINT64_MAX-page || a>UINT64_MAX-up(b)) return -22;
        auto end=a+up(b);
        for(const auto& view:shared_views) if(a<view.end && end>view.begin) {
            if(a<view.begin || end>view.end || !covered(anonymous,a,end)) return -22;
            if(c.Rax==10 && d&~1ull) return -13;
            DWORD old;
            if(!VirtualProtect(reinterpret_cast<void*>(a),static_cast<SIZE_T>(up(b)),c.Rax==11 || !d?PAGE_NOACCESS:PAGE_READONLY,&old)) return -12;
            replace_range(anonymous,a,end,static_cast<uint32_t>(d),c.Rax==11);return 0;
        }
        if(c.Rax==10 && stack_reservation && a>=stack_reservation && end<=stack_end && d<=3) {
            DWORD old;
            // ART's bottom-of-stack guard is separate from Windows' moving
            // guard page. Commit only the requested pages in this owned stack.
            if(!VirtualAlloc(reinterpret_cast<void*>(a),static_cast<SIZE_T>(up(b)),MEM_COMMIT,PAGE_READWRITE)) return -12;
            return VirtualProtect(reinterpret_cast<void*>(a),static_cast<SIZE_T>(up(b)),(d&2)?PAGE_READWRITE:(d&1)?PAGE_READONLY:PAGE_NOACCESS,&old)?0:-12;
        }
        std::vector<Range>* ranges=covered(anonymous,a,end)?&anonymous:covered(image->ranges,a,end)?&image->ranges:nullptr;
        if(!ranges) return -12;
        if(c.Rax==11) {
            if(cloned_thread && a<stack_end && end>stack_begin) { deferred_stack_unmaps.push_back({a,end,0});return 0; }
            if(ranges!=&anonymous) return -22;
            if(!VirtualFree(reinterpret_cast<void*>(a),static_cast<SIZE_T>(up(b)),MEM_DECOMMIT)) return -22;
            replace_range(*ranges,a,end,0,true);replace_range(executable_ranges,a,end,0,true);replace_range(zero_discardable,a,end,0,true);return 0;
        }
        if(d&~7ull || ((d&4) && !covered(image->ranges,a,end,4) && !covered(executable_ranges,a,end,4))) return -13;
        if(d&2) for(const auto* code:{&image->ranges,&executable_ranges})
            for(const auto& range:*code) if((range.prot&4) && a<range.end && end>range.begin) return -13;
        DWORD previous;
        if(ranges==&anonymous && d && !VirtualAlloc(reinterpret_cast<void*>(a),static_cast<SIZE_T>(up(b)),MEM_COMMIT,PAGE_READWRITE)) return -12;
        if((d&6)==6) return -13;
        if(!VirtualProtect(reinterpret_cast<void*>(a),static_cast<SIZE_T>(up(b)),(d&4)?PAGE_EXECUTE_READ:(d&2)?PAGE_READWRITE:(d&1)?PAGE_READONLY:PAGE_NOACCESS,&previous)) return -22;
        replace_range(*ranges,a,end,static_cast<uint32_t>(d));return 0;
    }
    case 25: { // MREMAP_MAYMOVE|MREMAP_FIXED for private data (linker CFI shadow).
        uint64_t target=c.R8;
        if(c.R10==1 && a%page==0 && b && d>=b && d<=512ull*1024*1024 && a<=UINT64_MAX-up(b) && covered(anonymous,a,a+up(b),1)) {
            uint64_t old_length=up(b),new_length=up(d);uint32_t prot=0;
            for(const auto& range:anonymous)if(range.begin<=a && range.end>a)prot=range.prot;
            for(const auto& range:anonymous)if(range.begin<a+old_length && range.end>a && range.prot!=prot)return -22;
            if(!prot || (prot&4))return -22;
            auto* moved=VirtualAlloc(nullptr,static_cast<SIZE_T>(new_length),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!moved)return -12;
            std::memcpy(moved,reinterpret_cast<void*>(a),static_cast<size_t>(old_length));DWORD old;
            if(!VirtualProtect(moved,static_cast<SIZE_T>(new_length),(prot&2)?PAGE_READWRITE:PAGE_READONLY,&old)){VirtualFree(moved,0,MEM_RELEASE);return -12;}
            auto destination=reinterpret_cast<uint64_t>(moved);allocations.insert(destination);
            anonymous.push_back({destination,destination+new_length,prot});
            if(covered(zero_discardable,a,a+old_length))zero_discardable.push_back({destination,destination+new_length,0});
            if(!VirtualFree(reinterpret_cast<void*>(a),static_cast<SIZE_T>(old_length),MEM_DECOMMIT))return -12;
            replace_range(anonymous,a,a+old_length,0,true);replace_range(zero_discardable,a,a+old_length,0,true);
            return static_cast<int64_t>(destination);
        }
        if(c.R10!=3 || !b || b!=d || a%page || target%page || b>64*1024*1024 || target>UINT64_MAX-up(b) || a>UINT64_MAX-up(b)) return -22;
        uint64_t length=up(b);
        if(a<target+length && target<a+length) return -22;
        if(!covered(anonymous,a,a+length,1) || !covered(anonymous,target,target+length)) return -14;
        uint32_t prot=0;
        for(const auto& r:anonymous) if(r.begin<=a && r.end>=a+length) prot=r.prot;
        if(!prot || (prot&4)) return -22;
        if(!VirtualAlloc(reinterpret_cast<void*>(target),static_cast<SIZE_T>(length),MEM_COMMIT,PAGE_READWRITE)) return -12;
        DWORD old;
        if(!VirtualProtect(reinterpret_cast<void*>(target),static_cast<SIZE_T>(length),PAGE_READWRITE,&old)) return -12;
        std::memcpy(reinterpret_cast<void*>(target),reinterpret_cast<void*>(a),static_cast<size_t>(length));
        if(!VirtualProtect(reinterpret_cast<void*>(target),static_cast<SIZE_T>(length),(prot&2)?PAGE_READWRITE:PAGE_READONLY,&old)) return -12;
        replace_range(anonymous,target,target+length,prot);
        if(!VirtualFree(reinterpret_cast<void*>(a),static_cast<SIZE_T>(length),MEM_DECOMMIT)) return -12;
        replace_range(anonymous,a,a+length,0,true);return target;
    }
    case 26: // Private mappings have no file writeback; report holes as Linux does.
        if(a%page || !b || b>UINT64_MAX-page || a>UINT64_MAX-up(b) || d&~7ull || (d&5)==5) return -22;
        return covered(anonymous,a,a+up(b)) || covered(image->ranges,a,a+up(b))?0:-12;
    case 28: {
        if(a%page || !b || b>UINT64_MAX-page || a>UINT64_MAX-up(b)) return -22;
        if(!covered(anonymous,a,a+up(b)) && !covered(image->ranges,a,a+up(b))) return -12;
        // Normal/random/sequential/willneed are advisory: keeping resident pages
        // is valid. Do not pretend MADV_DONTNEED discards private file mappings.
        if(d<=3) return 0;
        if(d!=4 || !covered(zero_discardable,a,a+up(b))) return -22;
        // Exception dispatch currently shares this stack. Refuse this optional
        // pthread stack-discard hint until syscalls have a separate host stack.
        if(a<stack_end && a+up(b)>stack_begin) return -22;
        for(const auto& range:anonymous) {
            auto begin=std::max<uint64_t>(a,range.begin),end=std::min<uint64_t>(a+up(b),range.end);
            if(begin>=end) continue;
            if(!VirtualFree(reinterpret_cast<void*>(begin),static_cast<SIZE_T>(end-begin),MEM_DECOMMIT)) return -12;
            if(range.prot && !VirtualAlloc(reinterpret_cast<void*>(begin),static_cast<SIZE_T>(end-begin),MEM_COMMIT,(range.prot&2)?PAGE_READWRITE:PAGE_READONLY)) return -12;
            recent_discards.push_back({begin,end,++discard_epoch});
            if(recent_discards.size()>256) recent_discards.erase(recent_discards.begin());
        }
        return 0;
    }
    case 35: {
        if(!access(a,sizeof(Timespec))) return -14;
        auto ts=*reinterpret_cast<Timespec*>(a);
        if(ts.sec<0 || ts.sec>3600 || ts.ns<0 || ts.ns>=1000000000) return -22;
        active_lock->unlock();Sleep(static_cast<DWORD>(ts.sec*1000+(ts.ns+999999)/1000000));active_lock->lock();return 0;
    }
    case 39: return GetCurrentProcessId();
    case 79:
        if(b<2) return -34;
        if(!access(a,2,true)) return -14;
        std::memcpy(reinterpret_cast<void*>(a),"/",2);return 2;
    case 97:
        if(!access(b,16,true)) return -14;
        if(a!=3) return -22;
        reinterpret_cast<uint64_t*>(b)[0]=8*1024*1024;
        reinterpret_cast<uint64_t*>(b)[1]=8*1024*1024;return 0;
    case 63: {
        if(!access(a,65*6,true)) return -14;
        auto* output=reinterpret_cast<char*>(a);std::memset(output,0,65*6);
        const char* fields[]={"Linux","axrb-native","6.1.0-axrb-userspace","AXRB Windows userspace ABI","x86_64","localdomain"};
        for(size_t i=0;i<6;i++) std::strcpy(output+i*65,fields[i]);return 0;
    }
    case 138: { // fstatfs, x86_64 ABI, a read-only Windows-backed virtual filesystem.
        auto* file=files.get(static_cast<int>(a));if(!file) return -9;
        if(!access(b,120,true)) return -14;
        auto* v=reinterpret_cast<uint64_t*>(b);std::memset(v,0,120);
        ULARGE_INTEGER available,total,free;
        auto root=files.host(file->path).root_path();
        if(!GetDiskFreeSpaceExW(root.c_str(),&available,&total,&free)) return -5;
        v[0]=0x41585242;v[1]=page;v[2]=total.QuadPart/page;v[3]=free.QuadPart/page;v[4]=available.QuadPart/page;
        v[8]=255;v[9]=page;v[10]=1;return 0;
    }
    case 72:
        if(a>2 && !files.get(static_cast<int>(a))) return -9;
        if(b==3) return a==1 || a==2?1:0; // F_GETFL, descriptors are read-only except stdout/stderr.
        if(b==1) return a<=2?0:files.get(static_cast<int>(a))->descriptor_flags;
        if(b==2 && a>2) { if(d&~1ull) return -22;files.get(static_cast<int>(a))->descriptor_flags=static_cast<uint32_t>(d);return 0; }
        return -22;
    case 96: {
        if(a && !access(a,16,true)) return -14;
        if(b && !access(b,8,true)) return -14;
        FILETIME t;GetSystemTimePreciseAsFileTime(&t);
        uint64_t us=(((uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime)-116444736000000000ULL)/10;
        if(a) { auto* v=reinterpret_cast<int64_t*>(a);v[0]=us/1000000;v[1]=us%1000000; }
        if(b) std::memset(reinterpret_cast<void*>(b),0,8);
        return 0;
    }
    case 318:
        if(d&~3ull || b>MAXDWORD) return -22;
        if(!access(a,b,true)) return -14;
        return BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(a),static_cast<ULONG>(b),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0?-5:static_cast<int64_t>(b);
    case 102: case 104: case 107: case 108: return 10000;
    case 145: return a==0 || guest_tids.count(a)?0:-3; // Main Linux TID equals PID.
    case 218:
        if(a && !access(a,4,true)) return -14;
        clear_tid=a;return guest_tid;
    case 186: return guest_tid;
    case 56: return clone_guest(c);
    case 202: return futex_guest(c);
    case 60: finish_guest(static_cast<DWORD>(a));
    case 231: if(active_lock && active_lock->owns_lock()) active_lock->unlock();ExitProcess(static_cast<DWORD>(a));
    case 158:
        if(a==0x1002) { if(!access(b,16)) return -14; guest_fs=b; return 0; }
        if(a==0x1003) { if(!access(b,8,true)) return -14; *reinterpret_cast<uint64_t*>(b)=guest_fs; return 0; }
        if(a==0x1001) { if(b && !access(b,8)) return -14;native_tls::gs_base=b;return 0; }
        if(a==0x1004) { if(!access(b,8,true)) return -14;*reinterpret_cast<uint64_t*>(b)=native_tls::gs_base;return 0; }
        return -1; // Never let a guest overwrite Windows' GS/TEB.
    case 228: {
        if(!access(b,sizeof(Timespec),true)) return -14;
        int64_t ns;
        if(a==1 || a==4 || a==6 || a==7) { LARGE_INTEGER t,f; QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f); ns=(t.QuadPart/f.QuadPart)*1000000000+(t.QuadPart%f.QuadPart)*1000000000/f.QuadPart; }
        else if(a==2 || a==3) {
            FILETIME created,ended,kernel,user;
            BOOL ok=a==2?GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user):GetThreadTimes(GetCurrentThread(),&created,&ended,&kernel,&user);
            if(!ok)return -5;
            uint64_t ticks=(uint64_t(kernel.dwHighDateTime)<<32|kernel.dwLowDateTime)+(uint64_t(user.dwHighDateTime)<<32|user.dwLowDateTime);
            ns=static_cast<int64_t>(ticks*100);
        }
        else if(a==0) { FILETIME t;GetSystemTimePreciseAsFileTime(&t);ns=static_cast<int64_t>(((uint64_t(t.dwHighDateTime)<<32|t.dwLowDateTime)-116444736000000000ULL)*100); }
        else return -22;
        *reinterpret_cast<Timespec*>(b)={ns/1000000000,ns%1000000000};return 0;
    }
    default: if(unknown_syscalls.insert(c.Rax).second) std::fprintf(stderr,"native-x64: unimplemented Linux syscall %llu\n",static_cast<unsigned long long>(c.Rax));return -38;
    }
}
LONG CALLBACK trap(EXCEPTION_POINTERS* exception) {
    auto& c=*exception->ContextRecord;
    if(exception->ExceptionRecord->ExceptionCode==0xe06d7363) return EXCEPTION_CONTINUE_SEARCH; // Host C++ exceptions.
    std::unique_lock<std::recursive_mutex> lock(kernel_mutex);active_lock=&lock;
    // A concurrent guest can touch a page between decommit and recommit in
    // MADV_DONTNEED. Once the syscall lock is acquired the discard is complete.
    // Retry only an accessible page from a completed discard, once per thread,
    // instruction, address and discard generation; never swallow a real fault.
    if(exception->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
       exception->ExceptionRecord->NumberParameters>=2 && return_stub && guest_fs) {
        const auto kind=exception->ExceptionRecord->ExceptionInformation[0];
        const auto address=exception->ExceptionRecord->ExceptionInformation[1];
        static thread_local uint64_t retried_epoch=0,retried_pc=0,retried_address=0;
        if(kind<=1 && covered(anonymous,address,address+1,kind?2:1)) {
            MEMORY_BASIC_INFORMATION info{};
            VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof info);
            if(info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
               (kind==0 || info.Protect==PAGE_READWRITE)) {
                for(auto it=recent_discards.rbegin();it!=recent_discards.rend();++it) {
                    if(address<it->begin || address>=it->end) continue;
                    if(retried_epoch==it->epoch && retried_pc==c.Rip && retried_address==address) break;
                    retried_epoch=it->epoch;retried_pc=c.Rip;retried_address=address;
                    std::fprintf(stderr,"native-x64: recovered concurrent discard fault at %llx epoch=%llu\n",address,it->epoch);
                    *reinterpret_cast<uint64_t*>(return_stub+page)=c.Rip;
                    *reinterpret_cast<uint64_t*>(return_stub+page+8)=c.Rcx;
                    c.Rip=reinterpret_cast<uint64_t>(return_stub);c.Rcx=guest_fs;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }
        }
    }
    if(exception->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION && guest_fs && return_stub &&
       (covered(image->ranges,c.Rip,c.Rip+15,4) || covered(executable_ranges,c.Rip,c.Rip+15,4))) {
        ZydisDecoder decoder;ZydisDecoderInit(&decoder,ZYDIS_MACHINE_MODE_LONG_64,ZYDIS_STACK_WIDTH_64);
        ZydisDecodedInstruction instruction;ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        if(ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder,reinterpret_cast<void*>(c.Rip),15,&instruction,operands))) {
            for(unsigned i=0;i<instruction.operand_count;i++) {
                const auto& op=operands[i];
                if(op.type!=ZYDIS_OPERAND_TYPE_MEMORY || op.mem.segment!=ZYDIS_REGISTER_FS || op.mem.base!=ZYDIS_REGISTER_NONE || op.mem.index!=ZYDIS_REGISTER_NONE) continue;
                auto offset=static_cast<uint64_t>(op.mem.disp.value);
                auto expected=guest_fs+offset;
                if(exception->ExceptionRecord->NumberParameters<2 || exception->ExceptionRecord->ExceptionInformation[1]!=offset ||
                   !access(expected,(op.size+7)/8,(op.actions&ZYDIS_OPERAND_ACTION_MASK_WRITE)!=0)) continue;
                // Windows may also clear FS on a scheduling transition. Retry
                // only a proven FS-relative fault whose guest address is valid.
                *reinterpret_cast<uint64_t*>(return_stub+page)=c.Rip;
                *reinterpret_cast<uint64_t*>(return_stub+page+8)=c.Rcx;
                c.Rip=reinterpret_cast<uint64_t>(return_stub);c.Rcx=guest_fs;++fs_recoveries;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }
    if(exception->ExceptionRecord->ExceptionCode==EXCEPTION_ILLEGAL_INSTRUCTION && image->gs_gates.count(c.Rip)) {
        std::fprintf(stderr,"native-x64: unsupported GS instruction at %llx:",c.Rip);
        for(auto byte:image->gs_gates.at(c.Rip)) std::fprintf(stderr," %02x",byte);
        std::fprintf(stderr,"; Windows GS is protected\n");
        ExitProcess(129);
    }
    if(exception->ExceptionRecord->ExceptionCode!=EXCEPTION_ILLEGAL_INSTRUCTION || !image->gates.count(c.Rip)) {
        std::fprintf(stderr,"native-x64 fault %08lx rip=%llx image+%llx address=%llx\n",exception->ExceptionRecord->ExceptionCode,c.Rip,c.Rip-reinterpret_cast<uintptr_t>(image->memory),exception->ExceptionRecord->NumberParameters>1?exception->ExceptionRecord->ExceptionInformation[1]:0);
        for(auto it=mapped_files.rbegin();it!=mapped_files.rend();++it)if(c.Rip>=it->begin && c.Rip<it->end){std::fprintf(stderr,"fault code %s file_offset=%llx mapping=%llx\n",it->path.c_str(),it->offset+c.Rip-it->begin,it->begin);break;}
        if(exception->ExceptionRecord->NumberParameters>1){auto address=exception->ExceptionRecord->ExceptionInformation[1];MEMORY_BASIC_INFORMATION info{};VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof info);std::fprintf(stderr,"fault data region=%p size=%llx protection=%lx state=%lx r14=%llx r15=%llx\n",info.BaseAddress,(uint64_t)info.RegionSize,info.Protect,info.State,c.R14,c.R15);for(auto& range:anonymous)if(address>=range.begin && address<range.end)std::fprintf(stderr,"fault logical mapping=%llx-%llx prot=%x\n",range.begin,range.end,range.prot);}
        std::fprintf(stderr,"fault registers rax=%llx rbx=%llx rcx=%llx rdx=%llx rdi=%llx rsi=%llx rbp=%llx rsp=%llx r8=%llx r9=%llx kind=%llu\n",c.Rax,c.Rbx,c.Rcx,c.Rdx,c.Rdi,c.Rsi,c.Rbp,c.Rsp,c.R8,c.R9,exception->ExceptionRecord->ExceptionInformation[0]);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    auto result=syscall(c);
    // NtContinue can reload the FS selector after the handler returns. Restore
    // the base in native code AFTER that transition. No stack/red-zone writes.
    *reinterpret_cast<uint64_t*>(return_stub+page)=c.Rip+2;
    *reinterpret_cast<uint64_t*>(return_stub+page+8)=c.Rip+2;
    c.Rip=reinterpret_cast<uint64_t>(return_stub);c.Rcx=guest_fs;c.R11=c.EFlags;c.Rax=static_cast<uint64_t>(result);
    return EXCEPTION_CONTINUE_EXECUTION;
}
void init_return_stub() {
    original_fs=_readfsbase_u64();
    return_stub=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!return_stub) throw std::runtime_error("Cannot initialize native TLS return stub");
    // wrfsbase rcx; mov rcx,[saved rcx]; jmp [resume PC]. No register/flags/stack loss.
    const uint8_t stub[]={0xf3,0x48,0x0f,0xae,0xd1,0x48,0x8b,0x0d,0xfc,0x0f,0,0,0xff,0x25,0xee,0x0f,0,0};
    std::memcpy(return_stub,stub,sizeof stub);DWORD previous;
    if(!VirtualProtect(return_stub,page,PAGE_EXECUTE_READ,&previous)) throw std::runtime_error("Cannot initialize native TLS return stub");
    FlushInstructionCache(GetCurrentProcess(),return_stub,sizeof stub);
}
DWORD WINAPI enter_clone(void* pointer) {
    if(trace_syscalls) std::fprintf(stderr,"native clone host entry tid=%lu\n",GetCurrentThreadId());
    std::unique_ptr<CloneStart> start(static_cast<CloneStart*>(pointer));
    init_return_stub();guest_tid=GetCurrentThreadId();cloned_thread=true;
    guest_fs=start->tls;native_tls::gs_base=start->gs;signal_mask=start->mask;
    clear_tid=(start->flags&0x200000)?start->tid_address:0;
    if(start->flags&0x1000000) *reinterpret_cast<uint32_t*>(start->tid_address)=static_cast<uint32_t>(guest_tid);
    NT_TIB* tib=reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    host_stack_begin=reinterpret_cast<uintptr_t>(tib->StackLimit);host_stack_end=reinterpret_cast<uintptr_t>(tib->StackBase);
    RtlCaptureContext(&host_exit_context);
    CONTEXT context=start->context;context.ContextFlags=CONTEXT_FULL;
    stack_begin=start->stack.begin;stack_end=start->stack.end;guest_initial_sp=context.Rsp;
    *reinterpret_cast<uint64_t*>(return_stub+page)=context.Rip;
    *reinterpret_cast<uint64_t*>(return_stub+page+8)=context.Rcx;
    context.Rip=reinterpret_cast<uint64_t>(return_stub);context.Rcx=guest_fs;
    start.reset();
    if(trace_syscalls) std::fprintf(stderr,"native clone resume tid=%llu rip=%llx rsp=%llx tls=%llx\n",guest_tid,*reinterpret_cast<uint64_t*>(return_stub+page),context.Rsp,guest_fs);
    tib->StackLimit=reinterpret_cast<void*>(stack_begin);tib->StackBase=reinterpret_cast<void*>(stack_end);
    RtlRestoreContext(&context,nullptr);return 127;
}
[[noreturn]] void WINAPI finish_on_host_stack(DWORD code) {
    {
        std::lock_guard guard(kernel_mutex);
        if(clear_tid && access(clear_tid,4,true)) {
            InterlockedExchange(reinterpret_cast<volatile LONG*>(clear_tid),0);futex_wake(clear_tid,INT32_MAX);
        }
        for(auto& r:deferred_stack_unmaps) {
            VirtualFree(reinterpret_cast<void*>(r.begin),static_cast<SIZE_T>(r.end-r.begin),MEM_DECOMMIT);
            replace_range(anonymous,r.begin,r.end,0,true);
            replace_range(zero_discardable,r.begin,r.end,0,true);
        }
        guest_tids.erase(guest_tid);
        signal_inboxes.erase(guest_tid);guest_stacks.erase(guest_tid);
    }
    _writefsbase_u64(original_fs);ExitThread(code);
}
[[noreturn]] void finish_guest(DWORD code) {
    if(active_lock && active_lock->owns_lock()) active_lock->unlock();
    if(!cloned_thread) finish_on_host_stack(code);
    auto* tib=reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    CONTEXT context=host_exit_context;
    context.Rip=reinterpret_cast<uint64_t>(&finish_on_host_stack);context.Rcx=code;
    context.Rsp=((context.Rsp-128)&~uint64_t(15))-8;
    *reinterpret_cast<uint64_t*>(context.Rsp)=0;
    tib->StackLimit=reinterpret_cast<void*>(host_stack_begin);tib->StackBase=reinterpret_cast<void*>(host_stack_end);
    RtlRestoreContext(&context,nullptr);ExitThread(code);
}
DWORD WINAPI enter(void*) {
    init_return_stub();
    guest_tid=GetCurrentProcessId();main_host_tid=GetCurrentThreadId();
    { std::lock_guard guard(kernel_mutex);guest_tids.insert(guest_tid); }
    // Keep the guest stack inside a real Windows thread's stack reservation, so
    // guard growth and exception dispatch still have correct TEB stack bounds.
    volatile uint8_t* scratch=static_cast<uint8_t*>(_alloca(65536));
    scratch[0]=0;
    NT_TIB* tib=reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    MEMORY_BASIC_INFORMATION stack_info{};VirtualQuery(const_cast<uint8_t*>(scratch),&stack_info,sizeof stack_info);
    stack_reservation=reinterpret_cast<uintptr_t>(stack_info.AllocationBase);
    stack_begin=reinterpret_cast<uintptr_t>(tib->StackLimit); stack_end=reinterpret_cast<uintptr_t>(tib->StackBase);
    { std::lock_guard guard(kernel_mutex);guest_stacks[guest_tid]={stack_begin,stack_end,3}; }
    auto sp=(reinterpret_cast<uintptr_t>(scratch)+32768)&~uintptr_t(15);
    guest_initial_sp=sp;
    auto* values=reinterpret_cast<uint64_t*>(sp);
    char* name=reinterpret_cast<char*>(sp+2048);std::strcpy(name,"axrb-native-probe");
    auto base=reinterpret_cast<uint64_t>(image->memory);
    auto random=sp+1024;
    if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(random),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) return 126;
    std::vector<uint64_t> initial={1,reinterpret_cast<uint64_t>(name),0,0};
    if(linker_mode) {
        std::strcpy(name,"/apex/com.android.runtime/bin/linker64");
        std::vector<std::string> args={guest_program.empty()?"--help":guest_program};
        args.insert(args.end(),program_args.begin(),program_args.end());
        initial={args.size()+1,reinterpret_cast<uint64_t>(name)};
        char* cursor=name+128;
        for(const auto& arg:args) {
            if(cursor+arg.size()+1>reinterpret_cast<char*>(sp+16384)) return 125;
            initial.push_back(reinterpret_cast<uint64_t>(cursor));std::memcpy(cursor,arg.c_str(),arg.size()+1);cursor+=arg.size()+1;
        }
        initial.push_back(0);
        const char* environment[]={
            "LD_LIBRARY_PATH=/apex/com.android.runtime/lib64/bionic:/apex/com.android.art/lib64:/apex/com.android.os.statsd/lib64:/apex/com.android.i18n/lib64:/system/lib64",
            "ANDROID_ROOT=/system","ANDROID_ART_ROOT=/apex/com.android.art","ANDROID_DATA=/data",
            "ANDROID_TZDATA_ROOT=/apex/com.android.tzdata","ANDROID_I18N_ROOT=/apex/com.android.i18n"};
        for(const char* env:environment) {
            initial.push_back(reinterpret_cast<uint64_t>(cursor));std::strcpy(cursor,env);cursor+=std::strlen(env)+1;
        }
        initial.push_back(0);
    }
    const uint64_t aux[]={3,base+image->h.phoff,4,sizeof(Program),5,image->h.phnum,6,page,7,0,9,base+image->h.entry,
        11,10000,12,10000,13,10000,14,10000,17,100,23,0,25,random,31,reinterpret_cast<uint64_t>(name),0,0};
    initial.insert(initial.end(),std::begin(aux),std::end(aux));
    std::memcpy(values,initial.data(),initial.size()*8);
    CONTEXT context{};RtlCaptureContext(&context);
    context.Rip=reinterpret_cast<uint64_t>(image->memory)+image->h.entry;context.Rsp=sp;context.Rbp=0;context.Rdx=0;
    RtlRestoreContext(&context,nullptr);
    return 127;
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try {
        if(argc<3 || (std::wcscmp(argv[1],L"--inspect") && std::wcscmp(argv[1],L"--run-probe") && std::wcscmp(argv[1],L"--run-linker"))) {
            std::fprintf(stderr,"usage: axrb-native-x64 --inspect ELF | --run-probe AUDITED_ELF | --run-linker X64_SYSROOT\n");return 2;
        }
        linker_mode=!std::wcscmp(argv[1],L"--run-linker");
        trace_syscalls=GetEnvironmentVariableW(L"AXRB_X64_TRACE",nullptr,0)!=0;
        if(!linker_mode && argc!=3) return 2;
        if(linker_mode) {
            files.init(argv[2]);
            if(argc>=4) { files.mount("/data/local/tmp/native-program",argv[3]);guest_program="/data/local/tmp/native-program"; }
            for(int i=4;i<argc;i++) program_args.push_back(std::filesystem::path(argv[i]).string());
        }
        auto path=linker_mode?std::filesystem::path(argv[2])/L"apex/com.android.runtime/bin/linker64":std::filesystem::path(argv[2]);
        Image loaded(path.c_str());image=&loaded;
        std::printf("ELF x86_64: type=%u entry=0x%llx interpreter=%s approved_probe=%s FSGSBASE=%s\n",loaded.h.type,
            static_cast<unsigned long long>(loaded.h.entry),loaded.interpreter.empty()?"none":loaded.interpreter.c_str(),loaded.approved?"yes":"no",IsProcessorFeaturePresent(22)?"yes":"no");
        if(!std::wcscmp(argv[1],L"--inspect")) return 0;
        if(!IsProcessorFeaturePresent(22)) throw std::runtime_error("Native FS TLS requires Windows FSGSBASE support; no hypervisor fallback");
        arm_bridge::prepare();
        loaded.load(linker_mode);
        if(linker_mode) {
            // Windows/loaded modules may already own pages below 2 GiB. A fixed
            // 32 MiB..1.75 GiB reservation failed when just one such page existed,
            // leaving ART in its expensive page-by-page fallback search.
            std::vector<Range> candidates;
            for(uintptr_t at=0x10000;at<0x70000000;) {
                MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof info))break;
                auto end=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
                auto begin=(std::max<uintptr_t>(at,reinterpret_cast<uintptr_t>(info.BaseAddress))+65535)&~uintptr_t(65535);
                auto stop=std::min<uintptr_t>(end,0x70000000)&~uintptr_t(65535);
                if(info.State==MEM_FREE && stop>begin)candidates.push_back({begin,stop,0});
                if(end<=at)break;at=end;
            }
            std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.end-a.begin>b.end-b.begin;});
            for(const auto& candidate:candidates) {
                auto size=candidate.end-candidate.begin;
                auto* allocation=VirtualAlloc(reinterpret_cast<void*>(candidate.begin),size,MEM_RESERVE,PAGE_NOACCESS);
                if(allocation){low_arena=reinterpret_cast<uintptr_t>(allocation);low_arena_size=size;break;}
            }
            if(!low_arena)throw std::runtime_error("No low-address arena available for Android compressed references");
            if(low_arena) allocations.insert(low_arena);
        }
        std::printf("Native x86 execution; %zu decoded syscall gates; no hypervisor; ARM bridge uses Digitalis\n",loaded.gates.size());std::fflush(stdout);
        auto handler=AddVectoredExceptionHandler(1,trap);
        if(!handler) throw std::runtime_error("Cannot register Linux syscall trap");
        HANDLE thread=CreateThread(nullptr,8*1024*1024,enter,nullptr,0,nullptr);
        if(!thread) throw std::runtime_error("Cannot create native guest thread");
        DWORD timeout=10000;char timeout_text[32]{};
        if(GetEnvironmentVariableA("AXRB_X64_TIMEOUT_MS",timeout_text,sizeof timeout_text)) {
            char* end=nullptr;auto requested=std::strtoul(timeout_text,&end,10);
            if(end && !*end && !requested)timeout=INFINITE; // Explicit interactive session.
            else if(end && !*end && requested>=1000 && requested<=600000)timeout=static_cast<DWORD>(requested);
        }
        if(WaitForSingleObject(thread,timeout)!=WAIT_OBJECT_0) {
            std::fprintf(stderr,"Native probe timed out\n");
            CONTEXT main_state{};main_state.ContextFlags=CONTEXT_CONTROL;
            SuspendThread(thread);GetThreadContext(thread,&main_state);ResumeThread(thread);
            std::fprintf(stderr,"main rip=%llx host_rva=%llx rsp=%llx syscall=%llu guest_pc=%llx calls=%llu\n",main_state.Rip,main_state.Rip-reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)),main_state.Rsp,last_main_syscall.load(),last_main_guest_pc.load(),main_syscall_count.load());
            std::fprintf(stderr,"main args=%llx,%llx,%llx\n",last_main_a.load(),last_main_b.load(),last_main_d.load());
            std::vector<HANDLE> diagnostic_handles;
            { std::unique_lock<std::recursive_mutex> guard(kernel_mutex,std::try_to_lock);
              if(guard.owns_lock())diagnostic_handles=clone_handles; }
            for(auto clone:diagnostic_handles) {
                DWORD code;GetExitCodeThread(clone,&code);CONTEXT state{};state.ContextFlags=CONTEXT_CONTROL;
                SuspendThread(clone);GetThreadContext(clone,&state);ResumeThread(clone);
                std::fprintf(stderr,"clone tid=%lu exit=%lu rip=%llx rsp=%llx\n",GetThreadId(clone),code,state.Rip,state.Rsp);
            }
            ExitProcess(124);
        }
        DWORD code;GetExitCodeThread(thread,&code);CloseHandle(thread);RemoveVectoredExceptionHandler(handler);
        for(auto p:allocations) VirtualFree(reinterpret_cast<void*>(p),0,MEM_RELEASE);
        for(const auto& view:shared_views) { UnmapViewOfFile(view.base);CloseHandle(view.section); }
        return static_cast<int>(code);
    } catch(const std::exception& error) { std::fprintf(stderr,"native-x64: %s\n",error.what());return 2; }
}

