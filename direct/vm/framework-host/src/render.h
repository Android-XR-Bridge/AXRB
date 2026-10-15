// Rendering for the framework host: Direct2D over WIC bitmaps, text through
// DirectWrite, image decoding and encoding through WIC. Internal to
// arun_framework_host.
//
// Every pixel buffer -- the window's surface and every android.graphics.Bitmap
// -- is a NativeBitmap (32bpp premultiplied BGRA). Java holds them as opaque
// handles; every handle crossing from Java is validated against the live set
// before use, so a forged handle is an exception, never a wild pointer.
//
// Ownership: Graphics owns every NativeBitmap until Free. Bitmaps owned by a
//   Java object are freed by the collector (Heap::RegisterNativeAllocation).
// Thread affinity: thread-safe; one lock serialises all drawing.
// Error propagation: false/nullptr plus an error string.
#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace arun::fw {

struct NativeBitmap;

// The canvas state a draw call runs under: an affine matrix in D2D order
// (m11 m12 m21 m22 dx dy), a device-space clip rectangle, a global alpha and
// the antialiasing flag.
struct DrawState {
  float m[6] = {1, 0, 0, 1, 0, 0};
  int clip[4] = {0, 0, 0, 0};
  float alpha = 1;
};

// A paint as the Java side packs it.
struct PaintSpec {
  std::uint32_t color = 0xff000000;
  int flags = 0;  // kPaint* below
  float stroke_width = 0;
  float miter = 4;
  std::span<const float> shader;  // empty: solid color
};
constexpr int kPaintAntiAlias = 0x1;
constexpr int kPaintStyleShift = 8;   // 0 fill, 1 stroke, 2 fill and stroke
constexpr int kPaintCapShift = 10;    // 0 butt, 1 round, 2 square
constexpr int kPaintJoinShift = 12;   // 0 miter, 1 round, 2 bevel
constexpr int kPaintUnderline = 0x10000;
constexpr int kPaintStrikeThru = 0x20000;

struct FontSpec {
  float size = 14;
  int weight = 400;
  int family = 0;  // 0 sans-serif, 1 serif, 2 monospace
  bool italic = false;
};

struct FontMetrics {
  float top, ascent, descent, bottom, leading;
};

class Graphics {
 public:
  // The process's graphics; nullptr (with `error`) if Direct2D is unavailable.
  static Graphics* Get(std::string* error);

  // --- bitmaps -------------------------------------------------------------
  NativeBitmap* CreateBitmap(int width, int height, bool has_alpha, std::string* error);
  // Decodes any image WIC can read. `max_w`/`max_h` > 0 scale the result to
  // exactly that size (density scaling, inSampleSize).
  NativeBitmap* Decode(std::span<const std::uint8_t> bytes, int scale_w, int scale_h, std::string* error);
  // Image size without decoding pixels; false if not an image.
  bool DecodeBounds(std::span<const std::uint8_t> bytes, int* width, int* height);
  NativeBitmap* Copy(NativeBitmap* src, int x, int y, int w, int h, int dst_w, int dst_h, bool filter,
                     std::string* error);
  void Free(NativeBitmap* b);
  // `handle` as a live bitmap, or nullptr.
  NativeBitmap* Validate(std::int64_t handle);
  static std::int64_t Handle(NativeBitmap* b) { return reinterpret_cast<std::intptr_t>(b); }

  int Width(NativeBitmap* b);
  int Height(NativeBitmap* b);
  bool HasAlpha(NativeBitmap* b);
  void SetHasAlpha(NativeBitmap* b, bool has_alpha);
  std::uint64_t Generation(NativeBitmap* b);
  // A pinned bitmap belongs to the host (the window surface): Java may draw
  // into it but never takes ownership of it.
  void Pin(NativeBitmap* b);
  bool Pinned(NativeBitmap* b);
  // Resizes (contents are lost; filled with `argb`).
  bool Resize(NativeBitmap* b, int width, int height, std::uint32_t argb, std::string* error);

  // Unpremultiplied ARGB access.
  bool GetPixels(NativeBitmap* b, int x, int y, int w, int h, std::uint32_t* out, int stride);
  bool SetPixels(NativeBitmap* b, int x, int y, int w, int h, const std::uint32_t* in, int stride);
  bool EraseColor(NativeBitmap* b, std::uint32_t argb);
  // Opaque top-down 0xAARRGGBB copy (window presentation).
  bool CopyOpaque(NativeBitmap* b, std::vector<std::uint32_t>* out);
  // format: 0 JPEG, 1 PNG. Empty on failure.
  std::vector<std::uint8_t> Encode(NativeBitmap* b, int format, int quality, std::string* error);
  std::string WritePng(NativeBitmap* b, const std::filesystem::path& path);

  // --- drawing -------------------------------------------------------------
  void Clear(NativeBitmap* t, const DrawState& s, std::uint32_t argb, bool src_mode);
  void DrawRect(NativeBitmap* t, const DrawState& s, float l, float top, float r, float b, float rx, float ry,
                const PaintSpec& p);
  void DrawOval(NativeBitmap* t, const DrawState& s, float l, float top, float r, float b, const PaintSpec& p);
  void DrawArc(NativeBitmap* t, const DrawState& s, float l, float top, float r, float b, float start, float sweep,
               bool use_center, const PaintSpec& p);
  void DrawLine(NativeBitmap* t, const DrawState& s, float x0, float y0, float x1, float y1, const PaintSpec& p);
  // Path ops: 0 move(x,y) 1 line(x,y) 2 quad(x1,y1,x2,y2) 3 cubic(x1..y3) 4 close.
  void DrawPath(NativeBitmap* t, const DrawState& s, std::span<const float> ops, bool even_odd, const PaintSpec& p);
  void DrawBitmap(NativeBitmap* t, const DrawState& s, NativeBitmap* src, const float srcr[4], const float dst[4],
                  int alpha, bool filter);
  void DrawText(NativeBitmap* t, const DrawState& s, std::u16string_view text, float x, float y,
                const FontSpec& f, const PaintSpec& p);

  // --- text ----------------------------------------------------------------
  float MeasureText(std::u16string_view text, const FontSpec& f);
  // Advance of every UTF-16 unit (0 for the trailing units of a cluster).
  void TextWidths(std::u16string_view text, const FontSpec& f, float* out);
  FontMetrics Metrics(const FontSpec& f);

 private:
  Graphics() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace arun::fw
