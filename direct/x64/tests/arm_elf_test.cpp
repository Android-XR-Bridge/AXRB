#include "../arm_elf.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
Bytes read(const std::string& path){std::ifstream file(path,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(file),{});}
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class F> void fails(F fn,const char* expected) {
    try{fn();}catch(const std::exception& e){check(std::string(e.what()).find(expected)!=std::string::npos,e.what());return;}
    throw std::runtime_error("invalid ARM group accepted");
}
int main(int argc,char** argv)try {
    check(argc==3,"parent and dependency paths required");auto parent=read(argv[1]),dep=read(argv[2]);
    auto reader=[&](const std::string& name){check(name=="/arm64/libarm-link-dep.so","dependency path");return dep;};
    for(int i=0;i<300;i++)fails([&]{arm_elf::load(parent.data(),parent.size(),"/arm64/parent.so",[](auto&){return Bytes{};});},"missing ARM dependency");
    auto group=arm_elf::load(parent.data(),parent.size(),"/arm64/parent.so",reader);
    check(group.size()==2 && group[0]->dependencies[0]==group[1].get(),"dependency graph");
    check(group[0]->initializers.size()==1 && group[1]->initializers.size()==1,"initializer lists");
    bool absolute=false,global=false,jump=false,weak=false;
    for(auto& r:group[0]->relocations) {
        uint32_t type=uint32_t(r.info);if(type==1027 || type==0)continue;
        auto& s=group[0]->dynamic_symbols[r.info>>32];uint64_t actual;std::memcpy(&actual,group[0]->memory+r.offset,8);
        auto it=group[1]->exports.find(s.name+"@"+s.version);
        if(s.name=="optional_value"){check(actual==0,"undefined weak import");weak=true;}
        else if(s.section==0){check(it!=group[1]->exports.end() && actual==it->second.address+uint64_t(r.addend),"relocation target");}
        absolute|=type==257;global|=type==1025;jump|=type==1026;
    }
    check(absolute && global && jump && weak,"fixture relocation coverage");
    auto broken=dep;
    for(size_t i=0;i+6<broken.size();i++)if(!std::memcmp(broken.data()+i,"AXRB_1",6))broken[i+5]='2';
    fails([&]{arm_elf::load(parent.data(),parent.size(),"/arm64/parent.so",[&](auto&){return broken;});},"unresolved ARM symbol");
    broken=dep;broken[18]=62;
    fails([&]{arm_elf::load(parent.data(),parent.size(),"/arm64/parent.so",[&](auto&){return broken;});},"expected ARM64");
    broken=dep;
    for(size_t i=0;i+10<broken.size();i++)if(!std::memcmp(broken.data()+i,"dep_answer",10))broken[i]='x';
    fails([&]{arm_elf::load(parent.data(),parent.size(),"/arm64/parent.so",[&](auto&){return broken;});},"unresolved ARM symbol dep_answer");
    fails([&]{arm_elf::load(parent.data(),32,"/arm64/parent.so",reader);},"truncated");
    for(size_t i=0;i+4<parent.size();i++)if(!std::memcmp(parent.data()+i,"APS2",4)) {
        auto packed=parent;packed[i]='X';
        fails([&]{arm_elf::load(packed.data(),packed.size(),"/arm64/parent.so",reader);},"APS2 header");
        packed=parent;packed[i+4]=0x80;packed[i+5]=0x80;packed[i+6]=0x80;packed[i+7]=0x7f;
        fails([&]{arm_elf::load(packed.data(),packed.size(),"/arm64/parent.so",reader);},"APS2 count");
        break;
    }
    std::cout<<"ARM_ELF_LINK_PASS relocation targets, versions, missing dependencies, rollback, wrong ISA\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
