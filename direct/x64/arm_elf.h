#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace arm_elf {
struct Area {uint64_t begin,end;uint32_t flags;};
struct Export {uint64_t address; bool weak;};
struct Symbol {std::string name;uint64_t address,size;uint16_t section;uint8_t info,visibility;std::string version;};
struct Relocation {uint64_t offset,info;int64_t addend;};
struct Image {
    uint8_t* memory=nullptr;uint64_t size=0;
    std::string path;
    std::vector<Area> areas;
    std::unordered_map<std::string,uint64_t> symbols;
    std::unordered_map<std::string,Export> exports;
    std::vector<Symbol> dynamic_symbols;
    std::vector<Relocation> relocations;
    std::vector<std::string> needed;
    std::vector<Image*> dependencies;
    std::vector<uint64_t> initializers;
    uint64_t init=0,init_array=0,init_size=0;
    bool active=false,callable=true;
    ~Image();
    bool mapped(uint64_t offset,uint64_t length,uint32_t flags=0) const;
};
using Reader=std::function<std::vector<uint8_t>(const std::string&)>;
// No objects are published until the entire local dependency group is linked.
// Each group is isolated. Dependencies are sibling ARM ELF files only; system
// namespace links and cross-ISA libc bridges are deliberately not synthesized.
std::vector<std::unique_ptr<Image>> load(const uint8_t*,size_t,const std::string&,const Reader&);
}
