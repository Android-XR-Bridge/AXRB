#include "arm_elf.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace arm_elf {
namespace {
constexpr uint64_t limit=512ull*1024*1024;
struct Header {uint8_t ident[16];uint16_t type,machine;uint32_t version;uint64_t entry,phoff,shoff;uint32_t flags;uint16_t size,phsize,phnum,shsize,shnum,shstr;};
struct Program {uint32_t type,flags;uint64_t offset,address,physical,filesize,memsize,align;};
struct Section {uint32_t name,type;uint64_t flags,address,offset,size;uint32_t link,info;uint64_t align,entsize;};
struct ElfSymbol {uint32_t name;uint8_t info,other;uint16_t section;uint64_t address,size;};
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class T> T get(const uint8_t* p){T value;std::memcpy(&value,p,sizeof value);return value;}
int64_t signed_add(int64_t a,int64_t b) {
    require(!(b>0 && a>INT64_MAX-b) && !(b<0 && a<INT64_MIN-b),"ARM packed relocation delta overflow");return a+b;
}
// APS2 format: signed LEB128 count/offset, followed by relocation groups.
// See Bionic linker_reloc_iterators.h for the four public group flag bits.
void unpack(Image& image,const uint8_t* data,size_t size) {
    require(size>=4 && !std::memcmp(data,"APS2",4),"invalid ARM APS2 header");size_t cursor=4;
    auto number=[&]()->int64_t {
        uint64_t value=0;
        for(unsigned i=0;i<10;i++) {
            require(cursor<size,"truncated ARM APS2 integer");uint8_t byte=data[cursor++];
            if(i==9)require(byte==0 || byte==0x7f,"overflowing ARM APS2 integer");
            value|=uint64_t(byte&0x7f)<<(i*7);
            if(!(byte&0x80)){if(i<9 && (byte&0x40))value|=UINT64_MAX<<((i+1)*7);return static_cast<int64_t>(value);}
        }
        throw std::runtime_error("unterminated ARM APS2 integer");
    };
    int64_t remaining=number(),offset=number(),addend=0;
    require(remaining>=0 && remaining<=1048576 && offset>=0,"invalid ARM APS2 count/offset");
    while(remaining) {
        int64_t count=number(),flags=number();require(count>0 && count<=remaining && flags>=0 && flags<=15 && !((flags&4) && !(flags&8)),"invalid ARM APS2 group");
        int64_t delta=(flags&2)?number():0;uint64_t info=(flags&1)?static_cast<uint64_t>(number()):0;
        if((flags&12)==12)addend=signed_add(addend,number());
        else if(!(flags&8))addend=0;
        for(int64_t i=0;i<count;i++) {
            offset=signed_add(offset,(flags&2)?delta:number());require(offset>=0,"negative ARM APS2 offset");
            if(!(flags&1))info=static_cast<uint64_t>(number());
            if((flags&12)==8)addend=signed_add(addend,number());
            image.relocations.push_back({static_cast<uint64_t>(offset),info,addend});
        }
        remaining-=count;
    }
    while(cursor<size)require(data[cursor++]==0,"trailing ARM APS2 data");
}
std::unique_ptr<Image> parse(const uint8_t* bytes,size_t length,const std::string& path) {
    auto valid=[&](uint64_t offset,uint64_t size){return offset<=length && size<=length-offset;};
    require(length<=limit && valid(0,sizeof(Header)),"truncated/oversized ARM ELF");
    auto h=get<Header>(bytes);
    require(!std::memcmp(h.ident,"\177ELF\2\1\1",7) && h.machine==183 && h.type==3 && h.version==1,"expected ARM64 ET_DYN ELF: "+path);
    require(h.phsize==sizeof(Program) && h.phnum && h.phnum<128 && valid(h.phoff,uint64_t(h.phnum)*sizeof(Program)),"invalid ARM program headers");
    require(h.shsize==sizeof(Section) && h.shnum && h.shnum<8192 && valid(h.shoff,uint64_t(h.shnum)*sizeof(Section)),"ARM symbol sections required");
    std::vector<Program> programs(h.phnum);std::memcpy(programs.data(),bytes+h.phoff,programs.size()*sizeof(Program));
    std::vector<Section> sections(h.shnum);std::memcpy(sections.data(),bytes+h.shoff,sections.size()*sizeof(Section));
    auto image=std::make_unique<Image>();image->path=path;
    for(const auto& p:programs) {
        require(p.type!=7,"ARM PT_TLS requires ARM Bionic TLS integration: "+path);
        require(p.type!=3,"ARM shared library has an interpreter");
        if(p.type!=1)continue;
        require(valid(p.offset,p.filesize) && p.filesize<=p.memsize && p.address<=limit && p.memsize<=limit-p.address,"invalid ARM load range");
        require((p.flags&3)!=3,"writable executable ARM segment");
        image->size=std::max(image->size,p.address+p.memsize);
    }
    require(image->size>0,"empty ARM library");image->size=(image->size+4095)&~uint64_t(4095);
    image->memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,static_cast<SIZE_T>(image->size),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    require(image->memory!=nullptr,"ARM image allocation failed");auto base=reinterpret_cast<uint64_t>(image->memory);
    for(const auto& p:programs)if(p.type==1 && p.memsize) {
        for(const auto& a:image->areas)require(base+p.address>=a.end || base+p.address+p.memsize<=a.begin,"overlapping ARM load segments");
        std::memcpy(image->memory+p.address,bytes+p.offset,static_cast<size_t>(p.filesize));
        image->areas.push_back({base+p.address,base+p.address+p.memsize,p.flags});
    }
    auto string_at=[&](const Section& strings,uint64_t index)->std::string {
        require(strings.type==3 && valid(strings.offset,strings.size) && index<strings.size,"invalid ARM string table");
        auto p=reinterpret_cast<const char*>(bytes+strings.offset+index);
        auto end=static_cast<const char*>(std::memchr(p,0,static_cast<size_t>(strings.size-index)));
        require(end!=nullptr,"unterminated ARM string");return std::string(p,end);
    };
    size_t dynsym=SIZE_MAX;
    for(size_t index=0;index<sections.size();++index) {
        const auto& s=sections[index];if(s.type!=8)require(valid(s.offset,s.size),"truncated ARM section");
        if(s.type!=11)continue;
        require(dynsym==SIZE_MAX && s.entsize==sizeof(ElfSymbol) && s.size%sizeof(ElfSymbol)==0 && s.link<sections.size(),"invalid ARM dynamic symbols");dynsym=index;
        for(uint64_t at=0;at<s.size;at+=sizeof(ElfSymbol)) {
            auto symbol=get<ElfSymbol>(bytes+s.offset+at);auto name=string_at(sections[s.link],symbol.name);
            image->dynamic_symbols.push_back({name,symbol.address,symbol.size,symbol.section,symbol.info,uint8_t(symbol.other&3),{}});
            unsigned type=symbol.info&15,binding=symbol.info>>4;
            if(!symbol.section || (type!=0 && type!=1 && type!=2))continue;
            require(symbol.section==0xfff1 || image->mapped(symbol.address,std::max<uint64_t>(1,symbol.size)),"ARM symbol outside image");
            uint64_t address=symbol.section==0xfff1?symbol.address:base+symbol.address;
            if((binding==1 || binding==2) && (symbol.other&3)!=1 && (symbol.other&3)!=2) {
                image->exports.emplace(name,Export{address,binding==2});
                if(type==2 && image->mapped(symbol.address,4,1))image->symbols.emplace(name,address);
            }
        }
    }
    for(const auto& s:sections) {
        if(s.type==6) {
            require(s.size%16==0 && s.link<sections.size(),"invalid ARM dynamic table");
            for(uint64_t at=0;at<s.size;at+=16) {
                auto tag=get<uint64_t>(bytes+s.offset+at),value=get<uint64_t>(bytes+s.offset+at+8);if(!tag)break;
                if(tag==1){auto name=string_at(sections[s.link],value);require(!name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-")==std::string::npos && name!="." && name!="..","invalid ARM dependency name");image->needed.push_back(name);}
                if(tag==12)image->init=value;
                if(tag==25)image->init_array=value;
                if(tag==27)image->init_size=value;
                require(!(tag==32 || tag==33) || !value,"ARM preinit array unsupported for shared library");
                require(tag!=22 && !(tag==30 && (value&4)),"ARM text relocations unsupported");
            }
        }
        if(s.type==4 && (s.flags&2)) {
            require(s.link==dynsym && s.entsize==sizeof(Relocation) && s.size%sizeof(Relocation)==0,"invalid ARM relocations");
            for(uint64_t at=0;at<s.size;at+=sizeof(Relocation))image->relocations.push_back(get<Relocation>(bytes+s.offset+at));
        }
        if((s.flags&2) && s.type==0x60000002) {
            require(s.link==dynsym,"invalid ARM APS2 symbol table");unpack(*image,bytes+s.offset,static_cast<size_t>(s.size));
        }
        if((s.flags&2) && (s.type==19 || s.type==0x6fffff00)) {
            require(s.size%8==0,"invalid ARM RELR table");uint64_t cursor=0;bool started=false;
            auto relative=[&](uint64_t offset){require(image->mapped(offset,8,2) && !(offset&7),"invalid ARM RELR target");image->relocations.push_back({offset,1027,get<int64_t>(image->memory+offset)});};
            for(uint64_t at=0;at<s.size;at+=8) {
                auto entry=get<uint64_t>(bytes+s.offset+at);
                if(!(entry&1)){relative(entry);cursor=entry+8;started=true;}
                else {
                    require(started && cursor<=UINT64_MAX-63*8,"invalid ARM RELR bitmap");
                    for(unsigned bit=1;bit<64;bit++)if(entry&(uint64_t(1)<<bit))relative(cursor+(bit-1)*8);
                    cursor+=63*8;
                }
            }
        }
        if(s.flags&2)require(s.type!=9 && s.type!=0x60000001,"ARM REL relocations unsupported");
    }
    std::unordered_map<uint16_t,std::string> versions;
    for(const auto& s:sections)if(s.type==0x6ffffffe || s.type==0x6ffffffd) {
        require(s.link<sections.size(),"invalid ARM version strings");
        uint64_t offset=0;
        for(uint32_t record=0;record<s.info;record++) {
            bool definition=s.type==0x6ffffffd;uint64_t header=definition?20:16;
            require(offset<=s.size && header<=s.size-offset,"truncated ARM version record");auto p=bytes+s.offset+offset;
            require(get<uint16_t>(p)==1,"unknown ARM symbol version format");
            auto count=get<uint16_t>(p+(definition?6:2));auto auxiliary=get<uint32_t>(p+(definition?12:8));auto next=get<uint32_t>(p+(definition?16:12));
            require(count && auxiliary>=header && auxiliary<=s.size-offset,"invalid ARM version auxiliary offset");
            uint64_t at=offset+auxiliary;
            for(uint16_t i=0;i<count;i++) {
                uint64_t entry=definition?8:16;require(at<=s.size && entry<=s.size-at,"truncated ARM version auxiliary");auto a=bytes+s.offset+at;
                auto index=definition?get<uint16_t>(p+4):get<uint16_t>(a+6);
                auto name=string_at(sections[s.link],get<uint32_t>(a+(definition?0:8)));
                if(!definition || i==0)versions[uint16_t(index&0x7fff)]=name;
                auto step=get<uint32_t>(a+(definition?4:12));
                if(i+1<count){require(step>=entry && step<=s.size-at,"invalid ARM version auxiliary chain");at+=step;}
            }
            if(record+1<s.info){require(next>=header && next<=s.size-offset,"invalid ARM version record chain");offset+=next;}
        }
    }
    for(const auto& s:sections)if(s.type==0x6fffffff) {
        require(s.link==dynsym && s.size==image->dynamic_symbols.size()*2,"invalid ARM version symbol table");
        image->exports.clear();image->symbols.clear();
        for(size_t i=0;i<image->dynamic_symbols.size();i++) {
            auto index=get<uint16_t>(bytes+s.offset+i*2);auto& symbol=image->dynamic_symbols[i];
            if((index&0x7fff)>1) {
                auto version=versions.find(uint16_t(index&0x7fff));require(version!=versions.end(),"unknown ARM symbol version index");symbol.version=version->second;
            }
            unsigned type=symbol.info&15,binding=symbol.info>>4;
            if(symbol.section && (index&0x7fff)!=0 && (type==0 || type==1 || type==2) && (binding==1 || binding==2) && symbol.visibility!=1 && symbol.visibility!=2) {
                auto address=symbol.section==0xfff1?symbol.address:base+symbol.address;Export value{address,binding==2};
                if(!symbol.version.empty())image->exports[symbol.name+"@"+symbol.version]=value;
                if(!(index&0x8000)) {
                    image->exports[symbol.name]=value;
                    if(type==2 && image->mapped(symbol.address,4,1))image->symbols[symbol.name]=address;
                }
            }
        }
    }
    return image;
}
void relocate(Image& image,const std::vector<Image*>& scope) {
    uint64_t base=reinterpret_cast<uint64_t>(image.memory);
    for(const auto& r:image.relocations) {
        uint32_t type=static_cast<uint32_t>(r.info);uint64_t index=r.info>>32,value=0;
        if(type==0)continue;
        require(image.mapped(r.offset,8,2) && !(r.offset&7),"ARM relocation destination is not writable/aligned");
        if(type==1027) {
            require(!index && r.addend>=0 && image.mapped(static_cast<uint64_t>(r.addend),1),"invalid ARM RELATIVE relocation");value=base+static_cast<uint64_t>(r.addend);
        } else {
            require(type==257 || type==1025 || type==1026,"unsupported ARM relocation type "+std::to_string(type)+": "+image.path);
            require(index<image.dynamic_symbols.size(),"invalid ARM relocation symbol index");const auto& s=image.dynamic_symbols[index];
            unsigned binding=s.info>>4,kind=s.info&15;
            require(kind==0 || kind==1 || kind==2,"ARM TLS/IFUNC relocation requires runtime support: "+s.name);
            bool found=false;
            if(s.section && (binding==0 || s.visibility)) {value=s.section==0xfff1?s.address:base+s.address;found=true;}
            else for(auto* dependency:scope) {
                auto key=s.version.empty()?s.name:s.name+"@"+s.version;
                auto it=dependency->exports.find(key);if(it==dependency->exports.end())continue;
                value=it->second.address;found=true;break;
            }
            require(found || binding==2,"unresolved ARM symbol "+s.name+(s.version.empty()?"":"@"+s.version)+" in "+image.path);
            value+=static_cast<uint64_t>(r.addend);
        }
        std::memcpy(image.memory+r.offset,&value,8);
    }
    if(image.init){require(image.mapped(image.init,4,1),"ARM DT_INIT outside code");image.initializers.push_back(base+image.init);}
    require(!(image.init_size%8) && image.init_size<=1024*1024 && (!image.init_size || image.mapped(image.init_array,image.init_size)),"invalid ARM init array");
    for(uint64_t at=0;at<image.init_size;at+=8) {
        auto pc=get<uint64_t>(image.memory+image.init_array+at);if(!pc || pc==UINT64_MAX)continue;
        bool executable=false;for(auto* dependency:scope)for(auto& area:dependency->areas)if((area.flags&1) && pc>=area.begin && pc<=area.end-4)executable=true;
        require(executable,"ARM initializer outside linked code");image.initializers.push_back(pc);
    }
    // ARM instructions remain non-executable data in the Windows process.
    DWORD old;require(VirtualProtect(image.memory,static_cast<SIZE_T>(image.size),PAGE_NOACCESS,&old)!=0,"ARM image protection failed");
    std::unordered_map<uint64_t,uint32_t> pages;
    for(auto& area:image.areas)for(auto page=(area.begin-base)&~uint64_t(4095);page<area.end-base;page+=4096)pages[page]|=area.flags;
    for(auto [page,flags]:pages)require(VirtualProtect(image.memory+page,4096,(flags&2)?PAGE_READWRITE:PAGE_READONLY,&old)!=0,"ARM segment protection failed");
}
}
Image::~Image(){if(memory)VirtualFree(memory,0,MEM_RELEASE);}
bool Image::mapped(uint64_t offset,uint64_t length,uint32_t flags) const {
    if(offset>size || length>size-offset)return false;auto base=reinterpret_cast<uint64_t>(memory);
    for(auto& area:areas)if(base+offset>=area.begin && base+offset+length<=area.end && (area.flags&flags)==flags)return true;
    return false;
}
std::vector<std::unique_ptr<Image>> load(const uint8_t* bytes,size_t length,const std::string& path,const Reader& reader) {
    std::vector<std::unique_ptr<Image>> images;std::unordered_map<std::string,Image*> names;uint64_t total=0;
    auto directory=path.substr(0,path.find_last_of('/')+1);
    std::function<Image*(const uint8_t*,size_t,const std::string&)> add=[&](const uint8_t* data,size_t size,const std::string& name)->Image* {
        auto existing=names.find(name);if(existing!=names.end())return existing->second;
        require(images.size()<128,"ARM dependency limit exceeded");
        auto image=parse(data,size,name);total+=image->size;require(total<=1024ull*1024*1024,"ARM dependency group exceeds 1 GiB");
        auto* result=image.get();names.emplace(name,result);images.push_back(std::move(image));
        for(auto& dependency:result->needed) {
            require(bool(reader),"ARM dependencies require path-based loading: "+dependency);
            auto child=directory+dependency;auto known=names.find(child);
            if(known!=names.end()){result->dependencies.push_back(known->second);continue;}
            auto content=reader(child);require(!content.empty(),"missing ARM dependency "+child+" required by "+name);
            result->dependencies.push_back(add(content.data(),content.size(),child));
        }
        return result;
    };
    add(bytes,length,path);
    // Android's local lookup group is breadth first, not recursive load order.
    std::vector<Image*> scope{images.front().get()};std::unordered_set<Image*> seen{scope.front()};
    for(size_t i=0;i<scope.size();++i)for(auto* child:scope[i]->dependencies)if(seen.insert(child).second)scope.push_back(child);
    for(auto& image:images)relocate(*image,scope);
    return images;
}
}
