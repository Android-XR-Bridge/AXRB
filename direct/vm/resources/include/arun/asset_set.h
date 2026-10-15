// The resource tables of one process, resolved together: the runtime's
// framework package (0x01) and the application's (0x7f). Implements what
// android::AssetManager2 and android::Theme do for the Java framework:
// configuration-aware lookup, reference and attribute resolution, bags (styles,
// arrays, plurals) with inheritance, themes, and the attribute resolution
// behind Theme.obtainStyledAttributes (AOSP AttributeResolution::ApplyStyle).
//
// Ownership: AssetSet borrows the tables; they must outlive it.
// Thread affinity: thread-safe (one lock).
// Error propagation: Result/Status; "not found" is std::nullopt, a malformed
//   table is a Status error.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "arun/res_table.h"
#include "arun/status.h"

namespace arun::res {

// A value together with where it came from.
struct Resolved {
  Value value;
  std::int32_t cookie = -1;       // table index; -1: not from a table (XML, or none)
  std::uint32_t resid = 0;        // the last resource reference followed
  std::uint32_t spec_flags = 0;   // configuration changes it depends on
  std::uint16_t density = 0;      // density of the chosen configuration
};

struct BagEntry {
  std::uint32_t key = 0;
  Value value;
  std::int32_t cookie = -1;
};

struct Bag {
  std::vector<BagEntry> entries;  // sorted by key, parents merged (child wins)
  std::uint32_t spec_flags = 0;
  const BagEntry* Find(std::uint32_t key) const;
};

// TypedArray layout (android.content.res.TypedArray STYLE_* indices).
constexpr int kStyleNumEntries = 7;

// One attribute of the XML element being inflated.
struct XmlAttr {
  std::uint32_t resource_id = 0;
  Value value;  // kTypeString data indexes the XML block's pool (cookie -1)
};

class AssetSet {
 public:
  // Adds a table; returns its cookie. Package ids must not repeat.
  Result<std::int32_t> AddTable(const ResourceTable* table);
  void SetConfig(const Config& config);
  Config config() const;
  const ResourceTable* table(std::int32_t cookie) const;

  // The best entry for `id` (a simple value; bags are not returned unless
  // `allow_bag`, in which case value is a reference to `id` itself).
  Result<std::optional<Resolved>> GetResource(std::uint32_t id, bool allow_bag = false,
                                              std::uint16_t density_override = 0);
  // Follows kTypeReference values (at most 20 hops). A reference to a bag stays
  // a reference. @null (reference 0) becomes kTypeNull/undefined.
  Status ResolveReference(Resolved* v);
  // The bag of `id` merged with its parents, or nullptr if `id` is not a bag.
  Result<const Bag*> GetBag(std::uint32_t id);

  std::optional<std::string> ResourceName(std::uint32_t id) const;
  std::uint32_t Identifier(std::string_view name, std::string_view default_type,
                           std::string_view default_package) const;
  // String `index` of table `cookie`'s value pool.
  Result<std::u16string> PooledString(std::int32_t cookie, std::uint32_t index) const;

  // --- themes -----------------------------------------------------------------
  // A theme is identified by the styles applied to it, in order, with their
  // force flags. Equal sequences share one theme id.
  Result<std::int32_t> Theme(std::span<const std::pair<std::uint32_t, bool>> styles);
  // The theme's value for `attr`, following ?attr chains inside the theme.
  Result<std::optional<Resolved>> ThemeAttribute(std::int32_t theme, std::uint32_t attr);
  // If `v` is a ?attr, replaces it with the theme's value; then resolves references.
  // Returns false (v unchanged) if the theme has no value for the attribute.
  Result<bool> ResolveAttributeReference(std::int32_t theme, Resolved* v);

  // Theme.obtainStyledAttributes: XML attributes, then the XML "style", then
  // defStyleAttr/defStyleRes, then the theme. `out_data` receives
  // kStyleNumEntries ints per attr; `out_indices` [count, indices...].
  // `xml_style` is the element's plain "style" attribute (kTypeNull if absent).
  Status ApplyStyle(std::int32_t theme, std::span<const XmlAttr> xml, Value xml_style,
                    std::uint32_t def_style_attr, std::uint32_t def_style_res, std::span<const std::uint32_t> attrs,
                    std::vector<std::int32_t>* out_data, std::vector<std::int32_t>* out_indices);
  // Resources.obtainAttributes: XML attributes only, references resolved, no theme.
  Status RetrieveAttributes(std::span<const XmlAttr> xml, std::span<const std::uint32_t> attrs,
                            std::vector<std::int32_t>* out_data, std::vector<std::int32_t>* out_indices);

 private:
  struct ThemeData {
    std::vector<std::pair<std::uint32_t, bool>> styles;
    std::uint64_t generation = 0;  // config generation the entries were built for
    std::unordered_map<std::uint32_t, BagEntry> entries;
    std::unordered_map<std::uint32_t, std::uint32_t> flags;
  };

  // Lock held for all of these.
  Result<std::optional<Resolved>> GetResourceLocked(std::uint32_t id, bool allow_bag, std::uint16_t density);
  Status ResolveReferenceLocked(Resolved* v);
  Result<const Bag*> GetBagLocked(std::uint32_t id, int depth);
  Status BuildTheme(ThemeData* t);
  Result<std::optional<Resolved>> ThemeAttributeLocked(ThemeData* t, std::uint32_t attr);
  Result<bool> ResolveAttributeLocked(ThemeData* t, Resolved* v);
  Result<ThemeData*> ThemeLocked(std::int32_t id);
  std::int32_t CookieFor(std::uint32_t id) const;
  static void Write(const Resolved& v, std::int32_t* out);

  mutable std::mutex lock_;
  std::vector<const ResourceTable*> tables_;
  std::int32_t cookie_by_package_[256];
  bool cookies_init_ = false;
  Config config_;
  std::uint64_t generation_ = 1;
  std::unordered_map<std::uint32_t, std::unique_ptr<Bag>> bags_;
  std::unordered_map<std::uint32_t, bool> not_bags_;
  std::vector<std::unique_ptr<ThemeData>> themes_;
  std::map<std::vector<std::pair<std::uint32_t, bool>>, std::int32_t> theme_ids_;
};

}  // namespace arun::res
