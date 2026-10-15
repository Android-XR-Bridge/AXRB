#include "arun/vm/class.h"

namespace arun::vm {

std::string RtMethod::PrettyName() const {
  std::string out = declaring != nullptr ? declaring->descriptor : std::string("?");
  out += '.';
  out += name;
  out += signature;
  return out;
}

bool RtClass::IsSubclassOf(const RtClass* base) const {
  for (const RtClass* c = this; c != nullptr; c = c->super) {
    if (c == base) return true;
  }
  return false;
}

bool RtClass::IsAssignableFrom(const RtClass* from) const {
  if (this == from) return true;
  if (from == nullptr || IsPrimitive() || from->IsPrimitive()) return false;
  // java.lang.Object: the only non-interface class without a superclass.
  if (super == nullptr && !IsInterface() && !IsArray()) return true;
  if (IsInterface()) {
    for (const RtClass* i : from->all_interfaces) {
      if (i == this) return true;
    }
    return false;
  }
  if (IsArray()) {
    if (!from->IsArray()) return false;
    if (component->IsPrimitive() || from->component->IsPrimitive()) return false;
    return component->IsAssignableFrom(from->component);
  }
  return from->IsSubclassOf(this);
}

RtMethod* RtClass::FindDeclaredMethod(std::string_view name, std::string_view signature) {
  for (RtMethod& m : methods) {
    if (m.name == name && m.signature == signature) return &m;
  }
  return nullptr;
}

RtField* RtClass::FindDeclaredField(std::string_view name, std::string_view type) {
  for (RtField& f : ifields) {
    if (f.name == name && f.type == type) return &f;
  }
  for (RtField& f : sfields) {
    if (f.name == name && f.type == type) return &f;
  }
  return nullptr;
}

std::string_view RtClass::PackageName() const {
  std::string_view d = descriptor;
  while (!d.empty() && d[0] == '[') d.remove_prefix(1);
  if (d.size() < 2 || d[0] != 'L') return {};
  const std::size_t slash = d.rfind('/');
  if (slash == std::string_view::npos) return {};
  return d.substr(1, slash - 1);
}

}  // namespace arun::vm
