#pragma once
template<class... T> constexpr void UNUSED(const T&...) {}
#define DISALLOW_COPY_AND_ASSIGN(Type) Type(const Type&) = delete; Type& operator=(const Type&) = delete
#define DISALLOW_IMPLICIT_CONSTRUCTORS(Type) Type() = delete; DISALLOW_COPY_AND_ASSIGN(Type)
#define DISALLOW_ALLOCATION() void* operator new(size_t) = delete; void operator delete(void*) = delete
