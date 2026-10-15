#include "arun/asset_set.h"

#include <algorithm>
#include <format>

namespace arun::res {
namespace {

constexpr int kMaxReferenceHops = 20;
constexpr int kMaxBagDepth = 32;
constexpr std::size_t kMaxThemes = 16384;

Status Error(std::string what) { return Err(ReasonCode::kManifestMalformed, "resources: " + what); }

// A dynamic reference (shared-library package id 0) points into the package
// that contains it.
void FixDynamic(Value* v, std::uint8_t package_id) {
  if (v->type == kTypeDynamicReference || v->type == kTypeDynamicAttribute) {
    v->type = v->type == kTypeDynamicReference ? kTypeReference : kTypeAttribute;
    if ((v->data >> 24) == 0 && v->data != 0) v->data |= static_cast<std::uint32_t>(package_id) << 24;
  }
}

}  // namespace

const BagEntry* Bag::Find(std::uint32_t key) const {
  auto it = std::lower_bound(entries.begin(), entries.end(), key,
                             [](const BagEntry& e, std::uint32_t k) { return e.key < k; });
  return it != entries.end() && it->key == key ? &*it : nullptr;
}

Result<std::int32_t> AssetSet::AddTable(const ResourceTable* table) {
  std::lock_guard<std::mutex> hold(lock_);
  if (!cookies_init_) {
    std::fill(std::begin(cookie_by_package_), std::end(cookie_by_package_), -1);
    cookies_init_ = true;
  }
  const auto cookie = static_cast<std::int32_t>(tables_.size());
  for (std::uint8_t id : table->PackageIds()) {
    if (cookie_by_package_[id] >= 0) return Error(std::format("package 0x{:02x} loaded twice", id));
  }
  for (std::uint8_t id : table->PackageIds()) cookie_by_package_[id] = cookie;
  tables_.push_back(table);
  ++generation_;
  bags_.clear();
  not_bags_.clear();
  return cookie;
}

void AssetSet::SetConfig(const Config& config) {
  std::lock_guard<std::mutex> hold(lock_);
  config_ = config;
  ++generation_;
  bags_.clear();
  not_bags_.clear();
}

Config AssetSet::config() const {
  std::lock_guard<std::mutex> hold(lock_);
  return config_;
}

const ResourceTable* AssetSet::table(std::int32_t cookie) const {
  std::lock_guard<std::mutex> hold(lock_);
  return cookie >= 0 && static_cast<std::size_t>(cookie) < tables_.size() ? tables_[cookie] : nullptr;
}

std::int32_t AssetSet::CookieFor(std::uint32_t id) const {
  return cookies_init_ ? cookie_by_package_[id >> 24] : -1;
}

// --- lookups ---------------------------------------------------------------------------

Result<std::optional<Resolved>> AssetSet::GetResource(std::uint32_t id, bool allow_bag, std::uint16_t density) {
  std::lock_guard<std::mutex> hold(lock_);
  return GetResourceLocked(id, allow_bag, density);
}

Result<std::optional<Resolved>> AssetSet::GetResourceLocked(std::uint32_t id, bool allow_bag, std::uint16_t density) {
  const std::int32_t cookie = CookieFor(id);
  if (cookie < 0) return std::optional<Resolved>();
  Config cfg = config_;
  if (density != 0) cfg.density = density;
  ARUN_ASSIGN_OR_RETURN(std::optional<Entry> e, tables_[cookie]->Find(id, cfg));
  if (!e) return std::optional<Resolved>();
  Resolved r;
  r.cookie = cookie;
  r.resid = id;
  r.spec_flags = e->spec_flags;
  r.density = e->config->density;
  if (e->complex) {
    if (!allow_bag) return std::optional<Resolved>();
    r.value = Value{kTypeReference, id};  // a bag is referred to, never inlined
    return std::optional<Resolved>(r);
  }
  r.value = e->value;
  FixDynamic(&r.value, static_cast<std::uint8_t>(id >> 24));
  return std::optional<Resolved>(r);
}

Status AssetSet::ResolveReference(Resolved* v) {
  std::lock_guard<std::mutex> hold(lock_);
  return ResolveReferenceLocked(v);
}

Status AssetSet::ResolveReferenceLocked(Resolved* v) {
  for (int hop = 0; hop < kMaxReferenceHops; ++hop) {
    if (v->value.type != kTypeReference) return Status::Ok();
    if (v->value.data == 0) {  // @null
      v->value = Value{kTypeNull, kDataNullUndefined};
      v->cookie = -1;
      return Status::Ok();
    }
    ARUN_ASSIGN_OR_RETURN(std::optional<Resolved> next, GetResourceLocked(v->value.data, true, 0));
    if (!next) return Status::Ok();  // unknown: stays a reference, the caller reports it
    if (next->value.type == kTypeReference && next->value.data == v->value.data) {
      // A bag: keep the reference, but remember where it lives.
      v->resid = v->value.data;
      v->cookie = next->cookie;
      v->spec_flags |= next->spec_flags;
      return Status::Ok();
    }
    const std::uint32_t flags = v->spec_flags | next->spec_flags;
    *v = *next;
    v->spec_flags = flags;
  }
  return Error(std::format("too many references resolving 0x{:08x}", v->value.data));
}

Result<const Bag*> AssetSet::GetBag(std::uint32_t id) {
  std::lock_guard<std::mutex> hold(lock_);
  return GetBagLocked(id, 0);
}

Result<const Bag*> AssetSet::GetBagLocked(std::uint32_t id, int depth) {
  if (auto it = bags_.find(id); it != bags_.end()) return static_cast<const Bag*>(it->second.get());
  if (not_bags_.count(id) != 0) return static_cast<const Bag*>(nullptr);
  if (depth > kMaxBagDepth) return Error(std::format("style 0x{:08x}: parents nested too deeply or cyclic", id));
  const std::int32_t cookie = CookieFor(id);
  if (cookie < 0) {
    not_bags_[id] = true;
    return static_cast<const Bag*>(nullptr);
  }
  ARUN_ASSIGN_OR_RETURN(std::optional<Entry> e, tables_[cookie]->Find(id, config_));
  if (!e || !e->complex) {
    not_bags_[id] = true;
    return static_cast<const Bag*>(nullptr);
  }
  auto bag = std::make_unique<Bag>();
  bag->spec_flags = e->spec_flags;
  const std::uint8_t package = static_cast<std::uint8_t>(id >> 24);
  std::uint32_t parent = e->parent;
  if (parent != 0 && (parent >> 24) == 0) parent |= static_cast<std::uint32_t>(package) << 24;
  if (parent != 0 && parent != id) {
    ARUN_ASSIGN_OR_RETURN(const Bag* p, GetBagLocked(parent, depth + 1));
    if (p != nullptr) {
      bag->entries = p->entries;
      bag->spec_flags |= p->spec_flags;
    }
  }
  std::vector<BagEntry> own;
  own.reserve(e->count);
  for (std::uint32_t i = 0; i < e->count; ++i) {
    auto [key, value] = e->BagPair(i);
    FixDynamic(&value, package);
    own.push_back(BagEntry{key, value, cookie});
  }
  std::stable_sort(own.begin(), own.end(), [](const BagEntry& a, const BagEntry& b) { return a.key < b.key; });
  for (const BagEntry& o : own) {
    auto it = std::lower_bound(bag->entries.begin(), bag->entries.end(), o.key,
                               [](const BagEntry& x, std::uint32_t k) { return x.key < k; });
    if (it != bag->entries.end() && it->key == o.key) *it = o; else bag->entries.insert(it, o);
  }
  const Bag* result = bag.get();
  bags_[id] = std::move(bag);
  return result;
}

std::optional<std::string> AssetSet::ResourceName(std::uint32_t id) const {
  std::lock_guard<std::mutex> hold(lock_);
  const std::int32_t cookie = CookieFor(id);
  if (cookie < 0) return std::nullopt;
  return tables_[cookie]->Name(id);
}

std::uint32_t AssetSet::Identifier(std::string_view name, std::string_view default_type,
                                   std::string_view default_package) const {
  if (!name.empty() && (name[0] == '@' || name[0] == '?')) name.remove_prefix(1);
  std::string_view package = default_package, type = default_type, entry = name;
  if (auto colon = entry.find(':'); colon != std::string_view::npos) {
    package = entry.substr(0, colon);
    entry.remove_prefix(colon + 1);
  }
  if (auto slash = entry.find('/'); slash != std::string_view::npos) {
    type = entry.substr(0, slash);
    entry.remove_prefix(slash + 1);
  }
  if (package.empty() || type.empty() || entry.empty()) return 0;
  std::lock_guard<std::mutex> hold(lock_);
  for (const ResourceTable* t : tables_) {
    if (std::uint32_t id = t->Identifier(package, type, entry); id != 0) return id;
  }
  return 0;
}

Result<std::u16string> AssetSet::PooledString(std::int32_t cookie, std::uint32_t index) const {
  std::lock_guard<std::mutex> hold(lock_);
  if (cookie < 0 || static_cast<std::size_t>(cookie) >= tables_.size()) return Error("bad asset cookie");
  std::u16string s;
  ARUN_RETURN_IF_ERROR(tables_[cookie]->strings().Get16(index, &s));
  return s;
}

// --- themes ------------------------------------------------------------------------------

Result<std::int32_t> AssetSet::Theme(std::span<const std::pair<std::uint32_t, bool>> styles) {
  std::lock_guard<std::mutex> hold(lock_);
  std::vector<std::pair<std::uint32_t, bool>> key(styles.begin(), styles.end());
  if (auto it = theme_ids_.find(key); it != theme_ids_.end()) return it->second;
  if (themes_.size() >= kMaxThemes) return Err(ReasonCode::kResourceExhausted, "too many distinct themes");
  auto t = std::make_unique<ThemeData>();
  t->styles = key;
  ARUN_RETURN_IF_ERROR(BuildTheme(t.get()));
  const auto id = static_cast<std::int32_t>(themes_.size());
  themes_.push_back(std::move(t));
  theme_ids_.emplace(std::move(key), id);
  return id;
}

Status AssetSet::BuildTheme(ThemeData* t) {
  t->entries.clear();
  t->flags.clear();
  for (const auto& [style, force] : t->styles) {
    ARUN_ASSIGN_OR_RETURN(const Bag* bag, GetBagLocked(style, 0));
    if (bag == nullptr) continue;  // Theme.applyStyle ignores what is not a style
    for (const BagEntry& e : bag->entries) {
      if (force || t->entries.count(e.key) == 0) {
        t->entries[e.key] = e;
        t->flags[e.key] = bag->spec_flags;
      }
    }
  }
  t->generation = generation_;
  return Status::Ok();
}

Result<AssetSet::ThemeData*> AssetSet::ThemeLocked(std::int32_t id) {
  if (id < 0 || static_cast<std::size_t>(id) >= themes_.size()) return Err(ReasonCode::kInvalidArgument, "bad theme");
  ThemeData* t = themes_[id].get();
  if (t->generation != generation_) ARUN_RETURN_IF_ERROR(BuildTheme(t));
  return t;
}

Result<std::optional<Resolved>> AssetSet::ThemeAttribute(std::int32_t theme, std::uint32_t attr) {
  std::lock_guard<std::mutex> hold(lock_);
  ARUN_ASSIGN_OR_RETURN(ThemeData * t, ThemeLocked(theme));
  return ThemeAttributeLocked(t, attr);
}

Result<std::optional<Resolved>> AssetSet::ThemeAttributeLocked(ThemeData* t, std::uint32_t attr) {
  std::uint32_t flags = 0;
  for (int hop = 0; hop < kMaxReferenceHops; ++hop) {
    auto it = t->entries.find(attr);
    if (it == t->entries.end()) return std::optional<Resolved>();
    flags |= t->flags[attr];
    const BagEntry& e = it->second;
    if (e.value.type == kTypeAttribute) {
      attr = e.value.data;
      continue;
    }
    if (e.value.type == kTypeNull && e.value.data != kDataNullEmpty) return std::optional<Resolved>();
    Resolved r;
    r.value = e.value;
    r.cookie = e.cookie;
    r.spec_flags = flags;
    return std::optional<Resolved>(r);
  }
  return std::optional<Resolved>();
}

Result<bool> AssetSet::ResolveAttributeReference(std::int32_t theme, Resolved* v) {
  std::lock_guard<std::mutex> hold(lock_);
  ARUN_ASSIGN_OR_RETURN(ThemeData * t, ThemeLocked(theme));
  return ResolveAttributeLocked(t, v);
}

Result<bool> AssetSet::ResolveAttributeLocked(ThemeData* t, Resolved* v) {
  if (v->value.type == kTypeAttribute) {
    ARUN_ASSIGN_OR_RETURN(std::optional<Resolved> r, ThemeAttributeLocked(t, v->value.data));
    if (!r) return false;
    const std::uint32_t flags = v->spec_flags | r->spec_flags;
    *v = *r;
    v->spec_flags = flags;
  }
  ARUN_RETURN_IF_ERROR(ResolveReferenceLocked(v));
  return true;
}

void AssetSet::Write(const Resolved& v, std::int32_t* out) {
  out[0] = v.value.type;
  out[1] = static_cast<std::int32_t>(v.value.data);
  out[2] = v.cookie;
  out[3] = static_cast<std::int32_t>(v.resid);
  out[4] = static_cast<std::int32_t>(v.spec_flags);
  out[5] = v.density;
  out[6] = 0;
}

Status AssetSet::ApplyStyle(std::int32_t theme, std::span<const XmlAttr> xml, Value xml_style,
                            std::uint32_t def_style_attr, std::uint32_t def_style_res,
                            std::span<const std::uint32_t> attrs, std::vector<std::int32_t>* out_data,
                            std::vector<std::int32_t>* out_indices) {
  std::lock_guard<std::mutex> hold(lock_);
  ARUN_ASSIGN_OR_RETURN(ThemeData * t, ThemeLocked(theme));

  std::uint32_t def_style = def_style_res;
  if (def_style_attr != 0) {
    ARUN_ASSIGN_OR_RETURN(std::optional<Resolved> r, ThemeAttributeLocked(t, def_style_attr));
    if (r && r->value.type == kTypeReference) def_style = r->value.data;
  }
  std::uint32_t style = 0;
  if (xml_style.type == kTypeAttribute) {
    ARUN_ASSIGN_OR_RETURN(std::optional<Resolved> r, ThemeAttributeLocked(t, xml_style.data));
    if (r && r->value.type == kTypeReference) style = r->value.data;
  } else if (xml_style.type == kTypeReference) {
    style = xml_style.data;
  }
  const Bag* def_bag = nullptr;
  const Bag* style_bag = nullptr;
  if (def_style != 0) {
    ARUN_ASSIGN_OR_RETURN(const Bag* b, GetBagLocked(def_style, 0));
    def_bag = b;
  }
  if (style != 0) {
    ARUN_ASSIGN_OR_RETURN(const Bag* b, GetBagLocked(style, 0));
    style_bag = b;
  }

  out_data->assign(attrs.size() * kStyleNumEntries, 0);
  out_indices->assign(1, 0);
  for (std::size_t i = 0; i < attrs.size(); ++i) {
    const std::uint32_t attr = attrs[i];
    Resolved v;
    v.value = Value{kTypeNull, kDataNullUndefined};
    std::uint32_t source = 0;
    for (const XmlAttr& a : xml) {
      if (a.resource_id == attr && attr != 0) {
        v.value = a.value;
        break;
      }
    }
    auto from_bag = [&](const Bag* bag, std::uint32_t bag_id) {
      if (bag == nullptr || v.value.type != kTypeNull || v.value.data == kDataNullEmpty) return;
      if (const BagEntry* e = bag->Find(attr)) {
        v.value = e->value;
        v.cookie = e->cookie;
        v.spec_flags |= bag->spec_flags;
        source = bag_id;
      }
    };
    from_bag(style_bag, style);
    from_bag(def_bag, def_style);

    if (v.value.type != kTypeNull) {
      ARUN_ASSIGN_OR_RETURN(bool ok, ResolveAttributeLocked(t, &v));
      if (!ok) {
        // An unresolvable ?attr: undefined, as on Android.
        v.value = Value{kTypeNull, kDataNullUndefined};
        v.cookie = -1;
      }
    } else if (v.value.data != kDataNullEmpty) {
      ARUN_ASSIGN_OR_RETURN(std::optional<Resolved> r, ThemeAttributeLocked(t, attr));
      if (r) {
        v = *r;
        ARUN_RETURN_IF_ERROR(ResolveReferenceLocked(&v));
      }
    }
    if (v.value.type == kTypeReference && v.value.data == 0) {
      v.value = Value{kTypeNull, kDataNullUndefined};
      v.cookie = -1;
    }
    std::int32_t* out = out_data->data() + i * kStyleNumEntries;
    Write(v, out);
    out[6] = static_cast<std::int32_t>(source);
    if (v.value.type != kTypeNull || v.value.data == kDataNullEmpty) {
      out_indices->push_back(static_cast<std::int32_t>(i));
      ++(*out_indices)[0];
    }
  }
  return Status::Ok();
}

Status AssetSet::RetrieveAttributes(std::span<const XmlAttr> xml, std::span<const std::uint32_t> attrs,
                                    std::vector<std::int32_t>* out_data, std::vector<std::int32_t>* out_indices) {
  std::lock_guard<std::mutex> hold(lock_);
  out_data->assign(attrs.size() * kStyleNumEntries, 0);
  out_indices->assign(1, 0);
  for (std::size_t i = 0; i < attrs.size(); ++i) {
    Resolved v;
    v.value = Value{kTypeNull, kDataNullUndefined};
    for (const XmlAttr& a : xml) {
      if (a.resource_id == attrs[i] && attrs[i] != 0) {
        v.value = a.value;
        break;
      }
    }
    ARUN_RETURN_IF_ERROR(ResolveReferenceLocked(&v));
    if (v.value.type == kTypeReference && v.value.data == 0) v.value = Value{kTypeNull, kDataNullUndefined};
    Write(v, out_data->data() + i * kStyleNumEntries);
    if (v.value.type != kTypeNull || v.value.data == kDataNullEmpty) {
      out_indices->push_back(static_cast<std::int32_t>(i));
      ++(*out_indices)[0];
    }
  }
  return Status::Ok();
}

}  // namespace arun::res
