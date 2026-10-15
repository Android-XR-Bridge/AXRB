#include "resources_host.h"

#include <cstring>
#include <format>

#include "arun/axml.h"
#include "arun/res_string_pool.h"
#include "arun/vm/runtime.h"
#include "arun/zip_reader.h"

namespace arun::fw {

using vm::Array;
using vm::ArgInt;
using vm::ArgRef;
using vm::Frame;
using vm::JValue;
using vm::Object;
using vm::RootScope;
using vm::Runtime;
using vm::RtClass;
using vm::ScopedBlocking;
using vm::Thread;

namespace {

constexpr std::uint64_t kMaxResourceFileBytes = 64ull << 20;
constexpr int kEventInts = 6;
constexpr int kAttrInts = 6;

ResourcesHost* g_res = nullptr;

Runtime* Rt(Thread* self) { return self->runtime(); }

// An int[] argument of at least `min` elements, or nullptr with an exception
// pending (null is allowed when `nullable`).
Array* IntArray(Thread* self, Frame* a, std::uint32_t reg, std::int32_t min, bool nullable = false) {
  auto* arr = static_cast<Array*>(ArgRef(a, reg));
  if (arr == nullptr) {
    if (!nullable) Rt(self)->ThrowNew(self, "Ljava/lang/NullPointerException;", "int[] argument");
    return nullptr;
  }
  if (arr->klass->descriptor != "[I" || arr->length < min) {
    Rt(self)->ThrowNew(self, "Ljava/lang/IllegalArgumentException;", "int[] argument too short");
    return nullptr;
  }
  return arr;
}

Array* NewIntArray(Thread* self, const std::vector<std::int32_t>& values) {
  Runtime* rt = Rt(self);
  RtClass* int_array = rt->class_linker().FindClass(self, "[I", nullptr);
  if (int_array == nullptr) return nullptr;
  Array* arr = rt->heap().AllocArray(self, int_array, static_cast<std::int32_t>(values.size()));
  if (arr != nullptr && !values.empty()) std::memcpy(arr->data(), values.data(), values.size() * 4);
  return arr;
}

void ThrowStatus(Thread* self, const Status& s) {
  Rt(self)->ThrowNew(self, "Ljava/lang/RuntimeException;", std::string(s.message()));
}

void WriteResolved(const res::Resolved& v, std::int32_t* out) {
  out[0] = v.value.type;
  out[1] = static_cast<std::int32_t>(v.value.data);
  out[2] = v.cookie;
  out[3] = static_cast<std::int32_t>(v.resid);
  out[4] = static_cast<std::int32_t>(v.spec_flags);
  out[5] = v.density;
  out[6] = 0;
}

// The XML attributes passed as three parallel int[] (ids, types, data).
bool XmlAttrs(Thread* self, Frame* a, std::uint32_t reg, std::vector<res::XmlAttr>* out) {
  out->clear();
  auto* ids = static_cast<Array*>(ArgRef(a, reg));
  if (ids == nullptr) return true;
  Array* types = IntArray(self, a, reg + 1, ids->length);
  Array* data = IntArray(self, a, reg + 2, ids->length);
  if (IntArray(self, a, reg, 0) == nullptr || types == nullptr || data == nullptr) return false;
  for (std::int32_t i = 0; i < ids->length; ++i) {
    res::XmlAttr x;
    x.resource_id = static_cast<std::uint32_t>(ids->elements<std::int32_t>()[i]);
    x.value.type = static_cast<std::uint8_t>(types->elements<std::int32_t>()[i]);
    x.value.data = static_cast<std::uint32_t>(data->elements<std::int32_t>()[i]);
    out->push_back(x);
  }
  return true;
}

// AssetManager.nativeSetConfiguration(mcc, mnc, locale, orientation, touchscreen,
//   density, keyboard, keyboardHidden, navigation, screenWidth, screenHeight,
//   smallestScreenWidthDp, screenWidthDp, screenHeightDp, screenLayout, uiMode,
//   colorMode, majorVersion)
void AssetManager_nativeSetConfiguration(Thread* self, Frame* a, JValue*) {
  res::Config c;
  c.mcc = static_cast<std::uint16_t>(ArgInt(a, 0));
  c.mnc = static_cast<std::uint16_t>(ArgInt(a, 1));
  const std::string locale = Rt(self)->GetStringUtf8(ArgRef(a, 2));
  const std::size_t sep = locale.find_first_of("-_");
  c.language = locale.substr(0, sep);
  if (sep != std::string::npos) {
    c.country = locale.substr(sep + 1);
    if (const std::size_t more = c.country.find_first_of("-_"); more != std::string::npos) c.country.resize(more);
  }
  c.orientation = static_cast<std::uint8_t>(ArgInt(a, 3));
  c.touchscreen = static_cast<std::uint8_t>(ArgInt(a, 4));
  c.density = static_cast<std::uint16_t>(ArgInt(a, 5));
  c.keyboard = static_cast<std::uint8_t>(ArgInt(a, 6));
  c.input_flags = static_cast<std::uint8_t>(ArgInt(a, 7) & 0x03);
  c.navigation = static_cast<std::uint8_t>(ArgInt(a, 8));
  c.screen_width = static_cast<std::uint16_t>(ArgInt(a, 9));
  c.screen_height = static_cast<std::uint16_t>(ArgInt(a, 10));
  c.smallest_screen_width_dp = static_cast<std::uint16_t>(ArgInt(a, 11));
  c.screen_width_dp = static_cast<std::uint16_t>(ArgInt(a, 12));
  c.screen_height_dp = static_cast<std::uint16_t>(ArgInt(a, 13));
  c.screen_layout = static_cast<std::uint8_t>(ArgInt(a, 14));
  c.ui_mode = static_cast<std::uint8_t>(ArgInt(a, 15));
  c.color_mode = static_cast<std::uint8_t>(ArgInt(a, 16));
  c.sdk_version = static_cast<std::uint16_t>(ArgInt(a, 17));
  g_res->assets().SetConfig(c);
}

// AssetManager.nativeGetResourceValue(int resid, int density, boolean resolve, int[] out7)
void AssetManager_nativeGetResourceValue(Thread* self, Frame* a, JValue* r) {
  r->i = 0;
  Array* out = IntArray(self, a, 3, res::kStyleNumEntries);
  if (out == nullptr) return;
  auto v = g_res->assets().GetResource(static_cast<std::uint32_t>(ArgInt(a, 0)), true,
                                       static_cast<std::uint16_t>(ArgInt(a, 1)));
  if (!v.ok()) return ThrowStatus(self, v.status());
  if (!*v) return;
  res::Resolved value = **v;
  if (ArgInt(a, 2) != 0) {
    if (Status s = g_res->assets().ResolveReference(&value); !s.ok()) return ThrowStatus(self, s);
  }
  WriteResolved(value, out->elements<std::int32_t>());
  r->i = 1;
}

// AssetManager.nativeGetBag(int resid): [spec_flags, count, (key, type, data, cookie)*] or null.
void AssetManager_nativeGetBag(Thread* self, Frame* a, JValue* r) {
  r->l = nullptr;
  auto bag = g_res->assets().GetBag(static_cast<std::uint32_t>(ArgInt(a, 0)));
  if (!bag.ok()) return ThrowStatus(self, bag.status());
  if (*bag == nullptr) return;
  std::vector<std::int32_t> v;
  v.reserve(2 + 4 * (*bag)->entries.size());
  v.push_back(static_cast<std::int32_t>((*bag)->spec_flags));
  v.push_back(static_cast<std::int32_t>((*bag)->entries.size()));
  for (const res::BagEntry& e : (*bag)->entries) {
    v.push_back(static_cast<std::int32_t>(e.key));
    v.push_back(e.value.type);
    v.push_back(static_cast<std::int32_t>(e.value.data));
    v.push_back(e.cookie);
  }
  r->l = NewIntArray(self, v);
}

void AssetManager_nativeGetResourceName(Thread* self, Frame* a, JValue* r) {
  r->l = nullptr;
  auto name = g_res->assets().ResourceName(static_cast<std::uint32_t>(ArgInt(a, 0)));
  if (name) r->l = Rt(self)->NewStringUtf8(self, *name);
}

void AssetManager_nativeGetIdentifier(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  r->i = static_cast<std::int32_t>(g_res->assets().Identifier(
      rt->GetStringUtf8(ArgRef(a, 0)), rt->GetStringUtf8(ArgRef(a, 1)), rt->GetStringUtf8(ArgRef(a, 2))));
}

void AssetManager_nativeGetPooledString(Thread* self, Frame* a, JValue* r) {
  r->l = nullptr;
  auto s = g_res->assets().PooledString(ArgInt(a, 0), static_cast<std::uint32_t>(ArgInt(a, 1)));
  if (!s.ok()) return ThrowStatus(self, s.status());
  r->l = Rt(self)->NewString(self, *s);
}

// AssetManager.nativeThemeCreate(int[] styleAndForcePairs)
void AssetManager_nativeThemeCreate(Thread* self, Frame* a, JValue* r) {
  Array* arr = IntArray(self, a, 0, 0);
  if (arr == nullptr) return;
  std::vector<std::pair<std::uint32_t, bool>> styles;
  for (std::int32_t i = 0; i + 1 < arr->length; i += 2) {
    styles.emplace_back(static_cast<std::uint32_t>(arr->elements<std::int32_t>()[i]),
                        arr->elements<std::int32_t>()[i + 1] != 0);
  }
  auto id = g_res->assets().Theme(styles);
  if (!id.ok()) return ThrowStatus(self, id.status());
  r->i = *id;
}

// AssetManager.nativeThemeGetAttribute(int theme, int attr, boolean resolve, int[] out7)
void AssetManager_nativeThemeGetAttribute(Thread* self, Frame* a, JValue* r) {
  r->i = 0;
  Array* out = IntArray(self, a, 3, res::kStyleNumEntries);
  if (out == nullptr) return;
  auto v = g_res->assets().ThemeAttribute(ArgInt(a, 0), static_cast<std::uint32_t>(ArgInt(a, 1)));
  if (!v.ok()) return ThrowStatus(self, v.status());
  if (!*v) return;
  res::Resolved value = **v;
  if (ArgInt(a, 2) != 0) {
    if (Status s = g_res->assets().ResolveReference(&value); !s.ok()) return ThrowStatus(self, s);
  }
  WriteResolved(value, out->elements<std::int32_t>());
  r->i = 1;
}

// AssetManager.nativeApplyStyle(theme, xmlIds, xmlTypes, xmlData, styleType, styleData,
//   defStyleAttr, defStyleRes, attrs, outData, outIndices)
void AssetManager_nativeApplyStyle(Thread* self, Frame* a, JValue*) {
  std::vector<res::XmlAttr> xml;
  if (!XmlAttrs(self, a, 1, &xml)) return;
  Array* attrs = IntArray(self, a, 8, 0);
  if (attrs == nullptr) return;
  Array* out_data = IntArray(self, a, 9, attrs->length * res::kStyleNumEntries);
  Array* out_indices = IntArray(self, a, 10, attrs->length + 1);
  if (out_data == nullptr || out_indices == nullptr) return;
  std::vector<std::uint32_t> ids(attrs->elements<std::uint32_t>(), attrs->elements<std::uint32_t>() + attrs->length);
  std::vector<std::int32_t> data, indices;
  const res::Value style{static_cast<std::uint8_t>(ArgInt(a, 4)), static_cast<std::uint32_t>(ArgInt(a, 5))};
  Status s = g_res->assets().ApplyStyle(ArgInt(a, 0), xml, style, static_cast<std::uint32_t>(ArgInt(a, 6)),
                                        static_cast<std::uint32_t>(ArgInt(a, 7)), ids, &data, &indices);
  if (!s.ok()) return ThrowStatus(self, s);
  std::memcpy(out_data->data(), data.data(), data.size() * 4);
  std::memcpy(out_indices->data(), indices.data(), indices.size() * 4);
}

// AssetManager.nativeRetrieveAttributes(xmlIds, xmlTypes, xmlData, attrs, outData, outIndices)
void AssetManager_nativeRetrieveAttributes(Thread* self, Frame* a, JValue*) {
  std::vector<res::XmlAttr> xml;
  if (!XmlAttrs(self, a, 0, &xml)) return;
  Array* attrs = IntArray(self, a, 3, 0);
  if (attrs == nullptr) return;
  Array* out_data = IntArray(self, a, 4, attrs->length * res::kStyleNumEntries);
  Array* out_indices = IntArray(self, a, 5, attrs->length + 1);
  if (out_data == nullptr || out_indices == nullptr) return;
  std::vector<std::uint32_t> ids(attrs->elements<std::uint32_t>(), attrs->elements<std::uint32_t>() + attrs->length);
  std::vector<std::int32_t> data, indices;
  Status s = g_res->assets().RetrieveAttributes(xml, ids, &data, &indices);
  if (!s.ok()) return ThrowStatus(self, s);
  std::memcpy(out_data->data(), data.data(), data.size() * 4);
  std::memcpy(out_indices->data(), indices.data(), indices.size() * 4);
}

// The bytes of `path` inside the package of `cookie`, or nullopt with an
// exception pending.
std::optional<std::vector<std::uint8_t>> ReadResourceFile(Thread* self, std::int32_t cookie, const std::string& path) {
  Runtime* rt = Rt(self);
  const pkg::ZipReader* zip = g_res->apk(cookie);
  const pkg::ZipEntry* e = zip == nullptr ? nullptr : zip->Find(path);
  if (e == nullptr || e->is_directory) {
    rt->ThrowNew(self, "Ljava/io/FileNotFoundException;", path);
    return std::nullopt;
  }
  if (e->uncompressed_size > kMaxResourceFileBytes) {
    rt->ThrowNew(self, "Ljava/io/IOException;", std::format("{} is too large", path));
    return std::nullopt;
  }
  std::optional<Result<std::vector<std::uint8_t>>> bytes;
  {
    ScopedBlocking blocking(self);
    bytes.emplace(zip->Extract(*e));
  }
  if (!bytes->ok()) {
    rt->ThrowNew(self, "Ljava/io/IOException;", std::format("{}: {}", path, bytes->status().message()));
    return std::nullopt;
  }
  return std::move(**bytes);
}

// AssetManager.nativeOpenNonAsset(int cookie, String path) -> byte[]
void AssetManager_nativeOpenNonAsset(Thread* self, Frame* a, JValue* r) {
  r->l = nullptr;
  auto bytes = ReadResourceFile(self, ArgInt(a, 0), Rt(self)->GetStringUtf8(ArgRef(a, 1)));
  if (!bytes) return;
  Runtime* rt = Rt(self);
  Array* arr = rt->heap().AllocArray(self, rt->wk.byte_array, static_cast<std::int32_t>(bytes->size()));
  if (arr == nullptr) return;
  if (!bytes->empty()) std::memcpy(arr->data(), bytes->data(), bytes->size());
  r->l = arr;
}

// AssetManager.nativeOpenXmlBlock(int cookie, String path) ->
//   Object[] {int[] events (6 per event), int[] attributes (6 per attribute), String[] strings}
void AssetManager_nativeOpenXmlBlock(Thread* self, Frame* a, JValue* r) {
  r->l = nullptr;
  Runtime* rt = Rt(self);
  const std::string path = rt->GetStringUtf8(ArgRef(a, 1));
  auto bytes = ReadResourceFile(self, ArgInt(a, 0), path);
  if (!bytes) return;
  auto doc = axml::ParseEvents(*bytes);
  if (!doc.ok()) {
    rt->ThrowNew(self, "Lorg/xmlpull/v1/XmlPullParserException;", std::format("{}: {}", path, doc.status().message()));
    return;
  }
  std::vector<std::int32_t> events, attrs;
  events.reserve(doc->events.size() * kEventInts);
  for (const axml::Event& e : doc->events) {
    events.insert(events.end(), {static_cast<std::int32_t>(e.kind), static_cast<std::int32_t>(e.line),
                                 static_cast<std::int32_t>(e.ns), static_cast<std::int32_t>(e.name),
                                 static_cast<std::int32_t>(e.first_attribute),
                                 static_cast<std::int32_t>(e.attribute_count)});
  }
  attrs.reserve(doc->attributes.size() * kAttrInts);
  for (const axml::EventAttribute& at : doc->attributes) {
    attrs.insert(attrs.end(), {static_cast<std::int32_t>(at.ns), static_cast<std::int32_t>(at.name),
                               static_cast<std::int32_t>(at.resource_id), static_cast<std::int32_t>(at.raw),
                               static_cast<std::int32_t>(at.type), static_cast<std::int32_t>(at.data)});
  }
  RootScope roots(self);
  auto result = roots.Add(rt->heap().AllocArray(self, rt->wk.object_array, 3));
  if (result.Get() == nullptr) return;
  Object* ev = NewIntArray(self, events);
  if (ev == nullptr) return;
  static_cast<Array*>(result.Get())->elements<Object*>()[0] = ev;
  Object* at = NewIntArray(self, attrs);
  if (at == nullptr) return;
  static_cast<Array*>(result.Get())->elements<Object*>()[1] = at;
  auto strings = roots.Add(rt->heap().AllocArray(self, rt->wk.string_array, static_cast<std::int32_t>(doc->pool.size())));
  if (strings.Get() == nullptr) return;
  static_cast<Array*>(result.Get())->elements<Object*>()[2] = strings.Get();
  std::u16string s;
  for (std::uint32_t i = 0; i < doc->pool.size(); ++i) {
    if (Status st = doc->pool.Get16(i, &s); !st.ok()) {
      rt->ThrowNew(self, "Lorg/xmlpull/v1/XmlPullParserException;", std::format("{}: {}", path, st.message()));
      return;
    }
    Object* str = rt->NewString(self, s);
    if (str == nullptr) return;
    static_cast<Array*>(strings.Get())->elements<Object*>()[i] = str;
  }
  r->l = result.Get();
}

}  // namespace

Result<std::unique_ptr<ResourcesHost>> ResourcesHost::Create(std::shared_ptr<pkg::ZipReader> framework,
                                                             std::shared_ptr<pkg::ZipReader> app) {
  if (framework == nullptr) return Status(ReasonCode::kInvalidArgument, "no framework resource package");
  std::unique_ptr<ResourcesHost> h(new ResourcesHost());
  h->apks_[0] = std::move(framework);
  h->apks_[1] = std::move(app);
  for (int i = 0; i < 2; ++i) {
    if (h->apks_[i] == nullptr) continue;
    const pkg::ZipEntry* e = h->apks_[i]->Find("resources.arsc");
    if (e == nullptr) {
      if (i == 0) return Status(ReasonCode::kInvalidArgument, "the framework package has no resources.arsc");
      continue;  // an application without resources
    }
    ARUN_ASSIGN_OR_RETURN(std::vector<std::uint8_t> bytes, h->apks_[i]->Extract(*e));
    ARUN_ASSIGN_OR_RETURN(h->tables_[i], res::ResourceTable::Load(bytes));
    ARUN_ASSIGN_OR_RETURN(std::int32_t cookie, h->assets_.AddTable(h->tables_[i].get()));
    if (cookie != i) return Status(ReasonCode::kInternal, "unexpected resource cookie");
  }
  return h;
}

const pkg::ZipReader* ResourcesHost::apk(int cookie) const {
  return cookie >= 0 && cookie < 2 ? apks_[cookie].get() : nullptr;
}

void ResourcesHost::RegisterNatives(Runtime* rt) {
  g_res = this;
  auto reg = [rt](const char* key, vm::NativeFn fn) { rt->RegisterNative(key, fn); };
  reg("Landroid/content/res/AssetManager;.nativeSetConfiguration(IILjava/lang/String;IIIIIIIIIIIIIII)V",
      AssetManager_nativeSetConfiguration);
  reg("Landroid/content/res/AssetManager;.nativeGetResourceValue(IIZ[I)Z", AssetManager_nativeGetResourceValue);
  reg("Landroid/content/res/AssetManager;.nativeGetBag(I)[I", AssetManager_nativeGetBag);
  reg("Landroid/content/res/AssetManager;.nativeGetResourceName(I)Ljava/lang/String;",
      AssetManager_nativeGetResourceName);
  reg("Landroid/content/res/AssetManager;.nativeGetIdentifier(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
      AssetManager_nativeGetIdentifier);
  reg("Landroid/content/res/AssetManager;.nativeGetPooledString(II)Ljava/lang/String;",
      AssetManager_nativeGetPooledString);
  reg("Landroid/content/res/AssetManager;.nativeThemeCreate([I)I", AssetManager_nativeThemeCreate);
  reg("Landroid/content/res/AssetManager;.nativeThemeGetAttribute(IIZ[I)Z", AssetManager_nativeThemeGetAttribute);
  reg("Landroid/content/res/AssetManager;.nativeApplyStyle(I[I[I[IIIII[I[I[I)V", AssetManager_nativeApplyStyle);
  reg("Landroid/content/res/AssetManager;.nativeRetrieveAttributes([I[I[I[I[I[I)V",
      AssetManager_nativeRetrieveAttributes);
  reg("Landroid/content/res/AssetManager;.nativeOpenNonAsset(ILjava/lang/String;)[B", AssetManager_nativeOpenNonAsset);
  reg("Landroid/content/res/AssetManager;.nativeOpenXmlBlock(ILjava/lang/String;)[Ljava/lang/Object;",
      AssetManager_nativeOpenXmlBlock);
}

}  // namespace arun::fw
