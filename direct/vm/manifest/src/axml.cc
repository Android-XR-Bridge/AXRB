#include "arun/axml.h"

#include <cstring>
#include <format>

namespace arun::axml {
namespace {

constexpr std::uint16_t kResStringPoolType = 0x0001;
constexpr std::uint16_t kResXmlType = 0x0003;
constexpr std::uint16_t kResXmlStartNamespace = 0x0100;
constexpr std::uint16_t kResXmlEndNamespace = 0x0101;
constexpr std::uint16_t kResXmlStartElement = 0x0102;
constexpr std::uint16_t kResXmlEndElement = 0x0103;
constexpr std::uint16_t kResXmlCdata = 0x0104;
constexpr std::uint16_t kResXmlResourceMap = 0x0180;
constexpr std::uint32_t kNoString = StringPool::kNoString;

Status Malformed(std::string what) { return Err(ReasonCode::kManifestMalformed, "binary XML: " + what); }

std::uint16_t U16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t U32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

Status CheckIndex(const StringPool& pool, std::uint32_t index) {
  if (index != kNoString && index >= pool.size()) return Malformed(std::format("string index {} out of range", index));
  return Status::Ok();
}

}  // namespace

const Attribute* Element::FindById(std::uint32_t resource_id) const {
  for (const Attribute& a : attributes) {
    if (a.resource_id == resource_id) return &a;
  }
  return nullptr;
}

const Attribute* Element::FindPlain(std::string_view n) const {
  for (const Attribute& a : attributes) {
    if (a.ns.empty() && a.name == n) return &a;
  }
  return nullptr;
}

Result<EventDocument> ParseEvents(std::span<const std::uint8_t> input, Limits limits) {
  if (input.size() < 8) return Malformed("file shorter than a chunk header");
  if (U16(input.data()) != kResXmlType) return Malformed("not a binary XML document");
  const std::uint16_t file_header = U16(input.data() + 2);
  const std::uint32_t file_size = U32(input.data() + 4);
  if (file_header < 8 || file_size < file_header || file_size > input.size()) return Malformed("document header");

  EventDocument doc;
  auto owned = std::make_shared<std::vector<std::uint8_t>>(input.begin(), input.begin() + file_size);
  doc.bytes = owned;
  const std::span<const std::uint8_t> bytes(*owned);

  bool have_pool = false;
  std::vector<std::uint32_t> resource_map;
  std::vector<std::uint32_t> open;  // tag name indices of open elements
  bool have_root = false;
  bool root_closed = false;
  std::uint32_t elements = 0;

  std::size_t pos = file_header;
  while (pos < bytes.size()) {
    if (bytes.size() - pos < 8) return Malformed("chunk header truncated");
    const std::uint8_t* h = bytes.data() + pos;
    const std::uint16_t type = U16(h);
    const std::uint16_t header_size = U16(h + 2);
    const std::uint32_t size = U32(h + 4);
    if (size < 8 || header_size < 8 || header_size > size || size > bytes.size() - pos) {
      return Malformed(std::format("chunk at {} has bad size", pos));
    }
    const std::span<const std::uint8_t> chunk = bytes.subspan(pos, size);
    pos += size;

    if (type == kResStringPoolType) {
      if (have_pool) return Malformed("second string pool");
      ARUN_RETURN_IF_ERROR(doc.pool.Load(chunk, limits.max_strings));
      have_pool = true;
      continue;
    }
    if (type == kResXmlResourceMap) {
      const std::size_t n = (size - header_size) / 4;
      if (n > limits.max_strings) return Malformed("resource map too large");
      resource_map.resize(n);
      for (std::size_t i = 0; i < n; ++i) resource_map[i] = U32(chunk.data() + header_size + 4 * i);
      continue;
    }
    if (type < kResXmlStartNamespace || type > kResXmlCdata) continue;  // unknown chunk: skip, bounded
    if (!have_pool) return Malformed("XML node before the string pool");
    if (header_size < 16) return Malformed("XML node header truncated");
    const std::uint32_t line = U32(chunk.data() + 8);
    const std::span<const std::uint8_t> ext = chunk.subspan(header_size);

    switch (type) {
      case kResXmlStartNamespace:
      case kResXmlEndNamespace:
        break;  // namespace URIs are recorded per attribute/element
      case kResXmlStartElement: {
        if (ext.size() < 20) return Malformed("element header truncated");
        if (root_closed) return Malformed("second root element");
        if (++elements > limits.max_elements) return Malformed("too many elements");
        if (open.size() >= limits.max_depth) return Malformed("elements nested too deeply");
        if (open.empty() && have_root) return Malformed("second root element");
        Event e;
        e.kind = Event::kStartTag;
        e.line = line;
        e.ns = U32(ext.data());
        e.name = U32(ext.data() + 4);
        ARUN_RETURN_IF_ERROR(CheckIndex(doc.pool, e.ns));
        if (e.name == kNoString) return Malformed("element without a name");
        ARUN_RETURN_IF_ERROR(CheckIndex(doc.pool, e.name));
        const std::uint16_t attr_start = U16(ext.data() + 8);
        const std::uint16_t attr_size = U16(ext.data() + 10);
        const std::uint16_t attr_count = U16(ext.data() + 12);
        if (attr_count > 0 && attr_size < 20) return Malformed("attribute size");
        if (static_cast<std::uint64_t>(attr_start) + static_cast<std::uint64_t>(attr_size) * attr_count > ext.size()) {
          return Malformed("attributes out of bounds");
        }
        e.first_attribute = static_cast<std::uint32_t>(doc.attributes.size());
        e.attribute_count = attr_count;
        for (std::uint16_t i = 0; i < attr_count; ++i) {
          const std::uint8_t* a = ext.data() + attr_start + static_cast<std::size_t>(attr_size) * i;
          EventAttribute attr;
          attr.ns = U32(a);
          attr.name = U32(a + 4);
          attr.raw = U32(a + 8);
          attr.type = static_cast<ValueType>(a[15]);
          attr.data = U32(a + 16);
          ARUN_RETURN_IF_ERROR(CheckIndex(doc.pool, attr.ns));
          ARUN_RETURN_IF_ERROR(CheckIndex(doc.pool, attr.name));
          ARUN_RETURN_IF_ERROR(CheckIndex(doc.pool, attr.raw));
          if (attr.type == ValueType::kString) ARUN_RETURN_IF_ERROR(CheckIndex(doc.pool, attr.data));
          if (attr.name != kNoString && attr.name < resource_map.size()) attr.resource_id = resource_map[attr.name];
          doc.attributes.push_back(attr);
        }
        doc.events.push_back(e);
        open.push_back(e.name);
        have_root = true;
        break;
      }
      case kResXmlEndElement: {
        if (open.empty()) return Malformed("end tag without start tag");
        if (ext.size() < 8) return Malformed("end element truncated");
        const std::uint32_t name = U32(ext.data() + 4);
        if (name != open.back()) {
          std::string closing, opened;
          (void)doc.pool.Get(name, &closing);
          (void)doc.pool.Get(open.back(), &opened);
          return Malformed(std::format("</{}> closes <{}>", closing, opened));
        }
        Event e;
        e.kind = Event::kEndTag;
        e.line = line;
        e.ns = U32(ext.data());
        e.name = name;
        ARUN_RETURN_IF_ERROR(CheckIndex(doc.pool, e.ns));
        doc.events.push_back(e);
        open.pop_back();
        if (open.empty()) root_closed = true;
        break;
      }
      case kResXmlCdata: {
        if (ext.size() < 4) return Malformed("character data truncated");
        if (open.empty()) break;  // whitespace outside the root
        Event e;
        e.kind = Event::kText;
        e.line = line;
        e.name = U32(ext.data());
        ARUN_RETURN_IF_ERROR(CheckIndex(doc.pool, e.name));
        doc.events.push_back(e);
        break;
      }
      default:
        break;
    }
  }
  if (!have_root) return Malformed("no root element");
  if (!open.empty()) {
    std::string name;
    (void)doc.pool.Get(open.back(), &name);
    return Malformed(std::format("<{}> is never closed", name));
  }
  return doc;
}

Result<Document> Parse(std::span<const std::uint8_t> bytes, Limits limits) {
  ARUN_ASSIGN_OR_RETURN(EventDocument events, ParseEvents(bytes, limits));
  const StringPool& pool = events.pool;
  Document doc;
  std::vector<Element*> stack;
  for (const Event& ev : events.events) {
    switch (ev.kind) {
      case Event::kStartTag: {
        Element e;
        e.line = ev.line;
        ARUN_RETURN_IF_ERROR(pool.Get(ev.ns, &e.ns));
        ARUN_RETURN_IF_ERROR(pool.Get(ev.name, &e.name));
        for (std::uint32_t i = 0; i < ev.attribute_count; ++i) {
          const EventAttribute& a = events.attributes[ev.first_attribute + i];
          Attribute attr;
          ARUN_RETURN_IF_ERROR(pool.Get(a.ns, &attr.ns));
          ARUN_RETURN_IF_ERROR(pool.Get(a.name, &attr.name));
          attr.resource_id = a.resource_id;
          if (a.raw != kNoString) {
            std::string r;
            ARUN_RETURN_IF_ERROR(pool.Get(a.raw, &r));
            attr.raw = std::move(r);
          }
          attr.type = a.type;
          attr.data = a.data;
          if (attr.type == ValueType::kString) ARUN_RETURN_IF_ERROR(pool.Get(attr.data, &attr.string_value));
          e.attributes.push_back(std::move(attr));
        }
        if (stack.empty()) {
          doc.root = std::move(e);
          stack.push_back(&doc.root);
        } else {
          Element* parent = stack.back();
          parent->children.push_back(std::move(e));
          stack.push_back(&parent->children.back());
        }
        break;
      }
      case Event::kEndTag:
        stack.pop_back();
        break;
      case Event::kText: {
        std::string text;
        ARUN_RETURN_IF_ERROR(pool.Get(ev.name, &text));
        stack.back()->text += text;
        break;
      }
    }
  }
  return doc;
}

}  // namespace arun::axml
