#include "arun/manifest.h"

#include <format>

namespace arun::manifest {
namespace {

// Framework attribute ids (android.R.attr), as stored in the resource map.
constexpr std::uint32_t kAttrLabel = 0x01010001;
constexpr std::uint32_t kAttrName = 0x01010003;
constexpr std::uint32_t kAttrExported = 0x01010010;
constexpr std::uint32_t kAttrEnabled = 0x0101000e;
constexpr std::uint32_t kAttrTheme = 0x01010000;
constexpr std::uint32_t kAttrHasCode = 0x0101000c;
constexpr std::uint32_t kAttrMinSdkVersion = 0x0101020c;
constexpr std::uint32_t kAttrTargetSdkVersion = 0x01010270;
constexpr std::uint32_t kAttrVersionCode = 0x0101021b;
constexpr std::uint32_t kAttrVersionName = 0x0101021c;
constexpr std::uint32_t kAttrTargetActivity = 0x01010202;
constexpr std::uint32_t kAttrVersionCodeMajor = 0x01010576;

constexpr char kMainAction[] = "android.intent.action.MAIN";
constexpr char kLauncherCategory[] = "android.intent.category.LAUNCHER";

Status Malformed(std::string what) { return Err(ReasonCode::kManifestMalformed, "AndroidManifest.xml: " + what); }

std::optional<std::string> StringOf(const axml::Attribute* a) {
  if (a == nullptr) return std::nullopt;
  if (a->IsString()) return a->string_value;
  if (a->raw) return a->raw;
  return std::nullopt;
}

std::optional<bool> BoolOf(const axml::Attribute* a) {
  if (a == nullptr) return std::nullopt;
  if (a->IsBoolean() || a->IsInt()) return a->data != 0;
  if (a->raw) {
    if (*a->raw == "true") return true;
    if (*a->raw == "false") return false;
  }
  return std::nullopt;
}

// minSdkVersion may be an integer or a codename string ("Q"); codenames mean
// a preview SDK, which this runner reports as unsupported by returning nullopt.
std::optional<int> IntOf(const axml::Attribute* a) {
  if (a == nullptr) return std::nullopt;
  if (a->IsInt()) return static_cast<int>(a->data);
  if (a->raw) {
    int v = 0;
    const std::string& s = *a->raw;
    if (s.empty()) return std::nullopt;
    for (char c : s) {
      if (c < '0' || c > '9' || v > 100000) return std::nullopt;
      v = v * 10 + (c - '0');
    }
    return v;
  }
  return std::nullopt;
}

TextValue TextOf(const axml::Attribute* a) {
  TextValue t;
  if (a == nullptr) return t;
  if (a->IsReference()) {
    t.resource_id = a->data;
  } else {
    t.literal = StringOf(a);
  }
  return t;
}

bool IsValidPackageName(std::string_view p) {
  // Two or more segments of [A-Za-z][A-Za-z0-9_]*, as PackageParser requires.
  int segments = 0;
  std::size_t i = 0;
  while (i < p.size()) {
    const char c = p[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    ++i;
    while (i < p.size() && p[i] != '.') {
      const char d = p[i];
      if (!((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9') || d == '_')) return false;
      ++i;
    }
    ++segments;
    if (i < p.size()) {
      ++i;  // '.'
      if (i == p.size()) return false;
    }
  }
  return segments >= 2;
}

}  // namespace

bool Activity::IsLauncher() const {
  if (!enabled) return false;
  for (const IntentFilter& f : intent_filters) {
    bool main = false, launcher = false;
    for (const std::string& a : f.actions) main |= a == kMainAction;
    for (const std::string& c : f.categories) launcher |= c == kLauncherCategory;
    if (main && launcher) return true;
  }
  return false;
}

std::string QualifyClassName(std::string_view package, std::string_view name) {
  if (name.empty()) return {};
  if (name[0] == '.') return std::string(package) + std::string(name);
  if (name.find('.') == std::string_view::npos) return std::string(package) + "." + std::string(name);
  return std::string(name);
}

Result<std::string> Manifest::LauncherActivity() const {
  for (const Activity& a : activities) {
    if (!a.IsLauncher()) continue;
    if (!a.is_alias) return a.name;
    for (const Activity& target : activities) {
      if (!target.is_alias && target.name == a.target_activity) return target.name;
    }
    return Err(ReasonCode::kManifestMalformed,
               std::format("launcher alias {} targets undeclared activity {}", a.name, a.target_activity));
  }
  return Err(ReasonCode::kManifestNoLauncher,
             "no enabled activity handles android.intent.action.MAIN with android.intent.category.LAUNCHER");
}

Result<Manifest> Parse(std::span<const std::uint8_t> binary_xml) {
  auto doc = axml::Parse(binary_xml);
  if (!doc.ok()) return doc.status();
  const axml::Element& root = doc->root;
  if (root.name != "manifest" || !root.ns.empty()) return Malformed("root element is not <manifest>");

  Manifest m;
  auto pkg = StringOf(root.FindPlain("package"));
  if (!pkg || !IsValidPackageName(*pkg)) {
    return Malformed(std::format("invalid package name \"{}\"", pkg.value_or("")));
  }
  m.package = *pkg;
  if (auto vc = IntOf(root.FindById(kAttrVersionCode))) m.version_code = static_cast<std::uint32_t>(*vc);
  if (auto major = IntOf(root.FindById(kAttrVersionCodeMajor))) {
    m.version_code |= static_cast<std::int64_t>(static_cast<std::uint32_t>(*major)) << 32;
  }
  m.version_name = StringOf(root.FindById(kAttrVersionName)).value_or("");

  for (const axml::Element& e : root.children) {
    if (e.name == "uses-sdk") {
      const axml::Attribute* min = e.FindById(kAttrMinSdkVersion);
      if (min != nullptr) {
        auto v = IntOf(min);
        if (!v) {
          return Err(ReasonCode::kManifestMalformed,
                     std::format("minSdkVersion \"{}\" is a preview codename, not an API level",
                                 StringOf(min).value_or("?")));
        }
        m.min_sdk = *v;
      }
      if (auto t = IntOf(e.FindById(kAttrTargetSdkVersion))) m.target_sdk = *t;
    } else if (e.name == "uses-permission" || e.name == "uses-permission-sdk-23") {
      if (auto n = StringOf(e.FindById(kAttrName))) m.uses_permissions.push_back(*n);
    } else if (e.name == "application") {
      if (auto n = StringOf(e.FindById(kAttrName))) m.application_class = QualifyClassName(m.package, *n);
      m.application_label = TextOf(e.FindById(kAttrLabel));
      if (const axml::Attribute* t = e.FindById(kAttrTheme); t != nullptr && t->IsReference()) {
        m.application_theme = t->data;
      }
      if (auto hc = BoolOf(e.FindById(kAttrHasCode))) m.has_code = *hc;
      for (const axml::Element& c : e.children) {
        if (c.name == "uses-library") {
          if (auto n = StringOf(c.FindById(kAttrName))) m.uses_libraries.push_back(*n);
          continue;
        }
        if (c.name == "uses-native-library") {
          if (auto n = StringOf(c.FindById(kAttrName))) m.uses_native_libraries.push_back(*n);
          continue;
        }
        if (c.name != "activity" && c.name != "activity-alias") continue;
        Activity a;
        a.is_alias = c.name == "activity-alias";
        auto n = StringOf(c.FindById(kAttrName));
        if (!n || n->empty()) return Malformed(std::format("<{}> at line {} has no android:name", c.name, c.line));
        a.name = QualifyClassName(m.package, *n);
        a.exported = BoolOf(c.FindById(kAttrExported));
        a.enabled = BoolOf(c.FindById(kAttrEnabled)).value_or(true);
        a.label = TextOf(c.FindById(kAttrLabel));
        if (const axml::Attribute* t = c.FindById(kAttrTheme); t != nullptr && t->IsReference()) a.theme = t->data;
        if (a.is_alias) {
          auto target = StringOf(c.FindById(kAttrTargetActivity));
          if (!target) return Malformed(std::format("<activity-alias {}> has no targetActivity", a.name));
          a.target_activity = QualifyClassName(m.package, *target);
        }
        for (const axml::Element& f : c.children) {
          if (f.name != "intent-filter") continue;
          IntentFilter filter;
          for (const axml::Element& x : f.children) {
            auto v = StringOf(x.FindById(kAttrName));
            if (!v) continue;
            if (x.name == "action") filter.actions.push_back(*v);
            if (x.name == "category") filter.categories.push_back(*v);
          }
          a.intent_filters.push_back(std::move(filter));
        }
        m.activities.push_back(std::move(a));
      }
    }
  }
  if (m.target_sdk == 0) m.target_sdk = m.min_sdk;
  return m;
}

}  // namespace arun::manifest
