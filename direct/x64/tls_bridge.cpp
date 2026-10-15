#include "tls_bridge.h"
#include <Zydis/Zydis.h>
#include <intrin.h>
#include <cstring>
#include <stdexcept>
#include <algorithm>

extern "C" unsigned long _tls_index;
namespace native_tls {
thread_local uint64_t gs_base=0;
namespace {
using Code=std::vector<uint8_t>;
void encode(Code& code,ZydisEncoderRequest request) {
    uint8_t buffer[ZYDIS_MAX_INSTRUCTION_LENGTH];ZyanUSize size=sizeof buffer;
    request.machine_mode=ZYDIS_MACHINE_MODE_LONG_64;
    if(!ZYAN_SUCCESS(ZydisEncoderEncodeInstruction(&request,buffer,&size))) throw std::runtime_error("Cannot encode native GS bridge");
    code.insert(code.end(),buffer,buffer+size);
}
ZydisEncoderOperand reg(ZydisRegister value) { ZydisEncoderOperand op{};op.type=ZYDIS_OPERAND_TYPE_REGISTER;op.reg.value=value;return op; }
ZydisEncoderOperand mem(ZydisRegister base,int64_t displacement) {
    ZydisEncoderOperand op{};op.type=ZYDIS_OPERAND_TYPE_MEMORY;op.mem.base=base;op.mem.displacement=displacement;op.mem.size=8;return op;
}
void binary(Code& code,ZydisMnemonic mnemonic,ZydisEncoderOperand left,ZydisEncoderOperand right,uint64_t prefixes=0) {
    ZydisEncoderRequest request{};request.mnemonic=mnemonic;request.operand_count=2;request.operands[0]=left;request.operands[1]=right;request.prefixes=prefixes;encode(code,request);
}
void unary(Code& code,ZydisMnemonic mnemonic,ZydisRegister value) {
    ZydisEncoderRequest request{};request.mnemonic=mnemonic;request.operand_count=1;request.operands[0]=reg(value);encode(code,request);
}
}
uint8_t* Batch::generate(const std::vector<uint8_t>& original,uintptr_t next) {
    if(original.size()<5) return nullptr;
    ZydisDecoder decoder;ZydisDecoderInit(&decoder,ZYDIS_MACHINE_MODE_LONG_64,ZYDIS_STACK_WIDTH_64);
    ZydisDecodedInstruction insn{};ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    if(!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder,original.data(),original.size(),&insn,ops))) return nullptr;
    const bool push_value=insn.mnemonic==ZYDIS_MNEMONIC_PUSH;
    switch(insn.mnemonic) {
    case ZYDIS_MNEMONIC_MOV:case ZYDIS_MNEMONIC_MOVZX:case ZYDIS_MNEMONIC_MOVSX:case ZYDIS_MNEMONIC_MOVSXD:
    case ZYDIS_MNEMONIC_CMP:case ZYDIS_MNEMONIC_TEST:case ZYDIS_MNEMONIC_ADD:case ZYDIS_MNEMONIC_SUB:
    case ZYDIS_MNEMONIC_AND:case ZYDIS_MNEMONIC_OR:case ZYDIS_MNEMONIC_XOR:case ZYDIS_MNEMONIC_INC:
    case ZYDIS_MNEMONIC_DEC:case ZYDIS_MNEMONIC_XCHG:case ZYDIS_MNEMONIC_CMPXCHG:case ZYDIS_MNEMONIC_PUSH:break;
    default:return nullptr;
    }
    int memory_operand=-1;
    bool store_sp=false;
    for(unsigned i=0;i<insn.operand_count;i++) {
        if(push_value && i>=insn.operand_count_visible) continue;
        if(ops[i].type==ZYDIS_OPERAND_TYPE_REGISTER && ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,ops[i].reg.value)==ZYDIS_REGISTER_RSP) {
            if(insn.mnemonic!=ZYDIS_MNEMONIC_MOV || i!=1 || ops[i].reg.value!=ZYDIS_REGISTER_RSP) return nullptr;
            store_sp=true;
        }
        if(ops[i].type==ZYDIS_OPERAND_TYPE_MEMORY) {
            if(memory_operand!=-1 || i>=insn.operand_count_visible || ops[i].mem.segment!=ZYDIS_REGISTER_GS) return nullptr;
            memory_operand=static_cast<int>(i);
        }
    }
    if(memory_operand<0) return nullptr;
    std::vector<ZydisRegister> scratch_registers;
    for(auto candidate:{ZYDIS_REGISTER_R11,ZYDIS_REGISTER_R10,ZYDIS_REGISTER_R9,ZYDIS_REGISTER_R8,ZYDIS_REGISTER_RDI,ZYDIS_REGISTER_RSI}) {
        bool used_register=false;
        for(unsigned i=0;i<insn.operand_count;i++) {
            if(ops[i].type==ZYDIS_OPERAND_TYPE_REGISTER && ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,ops[i].reg.value)==candidate) used_register=true;
            if(ops[i].type==ZYDIS_OPERAND_TYPE_MEMORY && (ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,ops[i].mem.base)==candidate || ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,ops[i].mem.index)==candidate)) used_register=true;
        }
        if(!used_register) scratch_registers.push_back(candidate);
    }
    if(scratch_registers.size()<2) return nullptr;
    auto scratch=scratch_registers[0],address_scratch=scratch_registers[1];
    auto& memory=ops[memory_operand].mem;
    bool indexed=memory.base!=ZYDIS_REGISTER_NONE || memory.index!=ZYDIS_REGISTER_NONE;
    Code code;
    // Keep the SysV red zone intact. LEA, MOV, PUSH and POP preserve flags.
    binary(code,ZYDIS_MNEMONIC_LEA,reg(ZYDIS_REGISTER_RSP),mem(ZYDIS_REGISTER_RSP,-128));
    unary(code,ZYDIS_MNEMONIC_PUSH,scratch);
    if(indexed || store_sp) unary(code,ZYDIS_MNEMONIC_PUSH,address_scratch);
    binary(code,ZYDIS_MNEMONIC_MOV,reg(scratch),mem(ZYDIS_REGISTER_NONE,0x58),ZYDIS_ATTRIB_HAS_SEGMENT_GS);
    binary(code,ZYDIS_MNEMONIC_MOV,reg(scratch),mem(scratch,uint64_t(_tls_index)*8));
    auto* blocks=reinterpret_cast<uintptr_t*>(__readgsqword(0x58));
    auto offset=reinterpret_cast<uintptr_t>(&gs_base)-blocks[_tls_index];
    if(offset>INT32_MAX) throw std::runtime_error("Native TLS offset exceeds bridge addressing range");
    binary(code,ZYDIS_MNEMONIC_MOV,reg(scratch),mem(scratch,static_cast<int64_t>(offset)));
    ZydisEncoderRequest request{};
    if(!ZYAN_SUCCESS(ZydisEncoderDecodedInstructionToEncoderRequest(&insn,ops,insn.operand_count_visible,&request))) return nullptr;
    request.prefixes&=~ZYDIS_ATTRIB_HAS_SEGMENT_GS;
    if(indexed) {
        ZydisEncoderRequest address{};address.mnemonic=ZYDIS_MNEMONIC_LEA;address.operand_count=2;
        auto address_reg=insn.address_width==32?ZydisRegisterEncode(ZYDIS_REGCLASS_GPR32,ZydisRegisterGetId(address_scratch)):address_scratch;
        address.operands[0]=reg(address_reg);address.operands[1]=request.operands[memory_operand];
        address.operands[1].mem.size=insn.address_width/8;
        address.address_size_hint=insn.address_width==32?ZYDIS_ADDRESS_SIZE_HINT_32:ZYDIS_ADDRESS_SIZE_HINT_64;
        if(memory.base==ZYDIS_REGISTER_RSP || memory.base==ZYDIS_REGISTER_ESP) address.operands[1].mem.displacement+=144;
        if(memory.base==ZYDIS_REGISTER_RIP || memory.base==ZYDIS_REGISTER_EIP) return nullptr;
        encode(code,address);
        auto sum=mem(scratch,0);sum.mem.index=address_scratch;sum.mem.scale=1;
        binary(code,ZYDIS_MNEMONIC_LEA,reg(scratch),sum);
        request.operands[memory_operand].mem.index=ZYDIS_REGISTER_NONE;
        request.operands[memory_operand].mem.scale=0;
        request.operands[memory_operand].mem.displacement=0;
    }
    request.address_size_hint=ZYDIS_ADDRESS_SIZE_HINT_64;
    request.operands[memory_operand].mem.base=scratch;
    if(store_sp) {
        binary(code,ZYDIS_MNEMONIC_LEA,reg(address_scratch),mem(ZYDIS_REGISTER_RSP,144));
        request.operands[1]=reg(address_scratch);
    }
    if(push_value) {
        if(request.operands[memory_operand].mem.size!=8) return nullptr;
        binary(code,ZYDIS_MNEMONIC_MOV,reg(scratch),request.operands[memory_operand]);
        binary(code,ZYDIS_MNEMONIC_MOV,mem(ZYDIS_REGISTER_RSP,indexed?136:128),reg(scratch));
    } else encode(code,request);
    if(indexed || store_sp) unary(code,ZYDIS_MNEMONIC_POP,address_scratch);
    unary(code,ZYDIS_MNEMONIC_POP,scratch);
    binary(code,ZYDIS_MNEMONIC_LEA,reg(ZYDIS_REGISTER_RSP),mem(ZYDIS_REGISTER_RSP,push_value?120:128));
    // RIP-indirect jump back preserves every guest register and the flags.
    code.insert(code.end(),{0xff,0x25,0,0,0,0});
    const auto* pc=reinterpret_cast<const uint8_t*>(&next);code.insert(code.end(),pc,pc+8);
    if(used+code.size()>65536) {
        uint8_t* block=nullptr;
        auto aligned=near_address&~uintptr_t(65535);
        for(uint64_t distance=65536;distance<0x70000000 && !block;distance+=65536) {
            for(bool above:{true,false}) {
                if(!above && aligned<distance+65536) continue;
                uintptr_t address=above?aligned+distance:aligned-distance;
                MEMORY_BASIC_INFORMATION info{};
                if(VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof info) && info.State==MEM_FREE)
                    block=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(address),65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
                if(block) break;
            }
        }
        if(!block) throw std::runtime_error("Cannot allocate GS bridge near guest code");
        pages.push_back(block);used=0;
    }
    auto* result=pages.back()+used;std::memcpy(result,code.data(),code.size());used=(used+code.size()+15)&~size_t(15);
    return result;
}
void Batch::seal() {
    for(auto* block:pages) {
        DWORD old;if(!VirtualProtect(block,65536,PAGE_EXECUTE_READ,&old)) throw std::runtime_error("Cannot seal GS bridge");
        FlushInstructionCache(GetCurrentProcess(),block,65536);
    }
    // Stubs live until this isolated process exits, including after dlclose.
}
}
