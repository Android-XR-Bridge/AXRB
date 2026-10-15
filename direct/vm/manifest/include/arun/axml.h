// Android binary XML ("AXML", aapt/aapt2 output): AndroidManifest.xml and
// compiled layout/xml resources.
//
// Ownership: XmlDocument is a value type; it copies every string it reports.
// Thread affinity: none. Error propagation: Result with kManifestMalformed.
//
// Hostile input: every offset and count is checked against the chunk that
// contains it, nesting depth and node counts are bounded, and nothing is read
// outside the input span. A malformed document is an error, never a partial
// tree presented as complete.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "arun/res_string_pool.h"
#include "arun/status.h"

namespace arun::axml {

// android::Res_value data types used in compiled XML.
enum class ValueType : std::uint8_t {
  kNull = 0x00,
  kReference = 0x01,
  kAttribute = 0x02,
  kString = 0x03,
  kFloat = 0x04,
  kDimension = 0x05,
  kFraction = 0x06,
  kDynamicReference = 0x07,
  kIntDec = 0x10,
  kIntHex = 0x11,
  kIntBoolean = 0x12,
  kIntColorArgb8 = 0x1c,
  kIntColorRgb8 = 0x1d,
  kIntColorArgb4 = 0x1e,
  kIntColorRgb4 = 0x1f,
};

struct Attribute {
  std::string ns;    // namespace URI, empty if none
  std::string name;
  std::uint32_t resource_id = 0;  // from the resource map; 0 if unmapped
  std::optional<std::string> raw;  // raw string value, if the compiler kept one
  ValueType type = ValueType::kNull;
  std::uint32_t data = 0;
  std::string string_value;  // for kString: the string (typed value), else empty

  bool IsString() const { return type == ValueType::kString; }
  bool IsBoolean() const { return type == ValueType::kIntBoolean; }
  bool IsInt() const { return type >= ValueType::kIntDec && type <= ValueType::kIntColorRgb4; }
  bool IsReference() const { return type == ValueType::kReference; }
};

struct Element {
  std::string ns;
  std::string name;
  std::uint32_t line = 0;
  std::vector<Attribute> attributes;
  std::vector<Element> children;
  std::string text;  // concatenated character data directly inside this element

  // The attribute with this framework resource id (e.g. 0x01010003 android:name).
  const Attribute* FindById(std::uint32_t resource_id) const;
  // The attribute with this local name and no namespace (e.g. "package").
  const Attribute* FindPlain(std::string_view name) const;
};

struct Document {
  Element root;
};

struct Limits {
  std::uint32_t max_depth = 128;
  std::uint32_t max_elements = 100000;
  std::uint32_t max_strings = 1u << 20;
};

Result<Document> Parse(std::span<const std::uint8_t> bytes, Limits limits = {});

// The same document as a flat event stream (what XmlPullParser walks), with
// names and values left as indices into the document's string pool.
struct EventAttribute {
  std::uint32_t ns = StringPool::kNoString;
  std::uint32_t name = StringPool::kNoString;
  std::uint32_t resource_id = 0;
  std::uint32_t raw = StringPool::kNoString;
  ValueType type = ValueType::kNull;
  std::uint32_t data = 0;
};

struct Event {
  enum Kind : std::uint8_t { kStartTag = 2, kEndTag = 3, kText = 4 };
  Kind kind = kStartTag;
  std::uint32_t line = 0;
  std::uint32_t ns = StringPool::kNoString;
  std::uint32_t name = StringPool::kNoString;  // tag name, or the text for kText
  std::uint32_t first_attribute = 0;
  std::uint32_t attribute_count = 0;
};

struct EventDocument {
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;  // the pool borrows these
  StringPool pool;
  std::vector<Event> events;
  std::vector<EventAttribute> attributes;
};

Result<EventDocument> ParseEvents(std::span<const std::uint8_t> bytes, Limits limits = {});

}  // namespace arun::axml
