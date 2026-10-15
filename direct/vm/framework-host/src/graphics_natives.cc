// Natives behind android.graphics.{Canvas, Paint, Bitmap, BitmapFactory}. Every
// bitmap handle from Java is validated (Graphics::Validate) before use.
#include "graphics_natives.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

#include "arun/vm/runtime.h"
#include "render.h"

namespace arun::fw {

using vm::Array;
using vm::ArgFloat;
using vm::ArgInt;
using vm::ArgLong;
using vm::ArgRef;
using vm::Frame;
using vm::JValue;
using vm::Object;
using vm::Runtime;
using vm::Thread;

namespace {

Graphics* g_gfx = nullptr;
int g_device_density = 160;

Runtime* Rt(Thread* self) { return self->runtime(); }

void Throw(Thread* self, const char* cls, std::string_view msg) { Rt(self)->ThrowNew(self, cls, msg); }

NativeBitmap* Bmp(Thread* self, std::int64_t handle) {
  NativeBitmap* b = g_gfx->Validate(handle);
  if (b == nullptr) Throw(self, "Ljava/lang/IllegalArgumentException;", "not a live bitmap");
  return b;
}

// A float[] argument with at least `min` elements.
const float* Floats(Thread* self, Frame* a, std::uint32_t reg, std::int32_t min) {
  auto* arr = static_cast<Array*>(ArgRef(a, reg));
  if (arr == nullptr || arr->klass->descriptor != "[F" || arr->length < min) {
    Throw(self, "Ljava/lang/IllegalArgumentException;", "float[] argument");
    return nullptr;
  }
  return arr->elements<float>();
}

// The optional shader description (null -> empty span).
std::span<const float> Shader(Frame* a, std::uint32_t reg) {
  auto* arr = static_cast<Array*>(ArgRef(a, reg));
  if (arr == nullptr || arr->klass->descriptor != "[F") return {};
  return std::span<const float>(arr->elements<float>(), static_cast<std::size_t>(arr->length));
}

bool State(Thread* self, Frame* a, std::uint32_t reg, DrawState* s) {
  const float* f = Floats(self, a, reg, 11);
  if (f == nullptr) return false;
  for (int i = 0; i < 6; ++i) s->m[i] = f[i];
  for (int i = 0; i < 4; ++i) {
    const float v = f[6 + i];
    s->clip[i] = std::isfinite(v) ? static_cast<int>(std::clamp(v, -1e6f, 1e6f)) : 0;
  }
  s->alpha = std::clamp(f[10], 0.0f, 1.0f);
  return true;
}

// Paint arguments starting at `reg`: color, flags, strokeWidth, miter, shader.
PaintSpec Paint(Frame* a, std::uint32_t reg) {
  PaintSpec p;
  p.color = static_cast<std::uint32_t>(ArgInt(a, reg));
  p.flags = ArgInt(a, reg + 1);
  p.stroke_width = ArgFloat(a, reg + 2);
  p.miter = ArgFloat(a, reg + 3);
  if (!std::isfinite(p.stroke_width) || p.stroke_width < 0) p.stroke_width = 0;
  if (!std::isfinite(p.miter)) p.miter = 4;
  p.shader = Shader(a, reg + 4);
  return p;
}

FontSpec Font(Frame* a, std::uint32_t reg) {
  FontSpec f;
  f.size = ArgFloat(a, reg);
  if (!std::isfinite(f.size) || f.size <= 0) f.size = 1;
  f.size = std::min(f.size, 2048.0f);
  f.weight = ArgInt(a, reg + 1);
  f.family = ArgInt(a, reg + 2);
  f.italic = ArgInt(a, reg + 3) != 0;
  return f;
}

// --- Canvas ---------------------------------------------------------------------------
// Static natives: (long target) occupies registers 0-1, the state array is 2.

void Canvas_nativeClear(Thread* self, Frame* a, JValue*) {
  DrawState s;
  NativeBitmap* t = Bmp(self, ArgLong(a, 0));
  if (t == nullptr || !State(self, a, 2, &s)) return;
  g_gfx->Clear(t, s, static_cast<std::uint32_t>(ArgInt(a, 3)), ArgInt(a, 4) != 0);
}

void Canvas_nativeDrawRect(Thread* self, Frame* a, JValue*) {
  DrawState s;
  NativeBitmap* t = Bmp(self, ArgLong(a, 0));
  if (t == nullptr || !State(self, a, 2, &s)) return;
  g_gfx->DrawRect(t, s, ArgFloat(a, 3), ArgFloat(a, 4), ArgFloat(a, 5), ArgFloat(a, 6), ArgFloat(a, 7), ArgFloat(a, 8),
                  Paint(a, 9));
}

void Canvas_nativeDrawOval(Thread* self, Frame* a, JValue*) {
  DrawState s;
  NativeBitmap* t = Bmp(self, ArgLong(a, 0));
  if (t == nullptr || !State(self, a, 2, &s)) return;
  g_gfx->DrawOval(t, s, ArgFloat(a, 3), ArgFloat(a, 4), ArgFloat(a, 5), ArgFloat(a, 6), Paint(a, 7));
}

void Canvas_nativeDrawArc(Thread* self, Frame* a, JValue*) {
  DrawState s;
  NativeBitmap* t = Bmp(self, ArgLong(a, 0));
  if (t == nullptr || !State(self, a, 2, &s)) return;
  g_gfx->DrawArc(t, s, ArgFloat(a, 3), ArgFloat(a, 4), ArgFloat(a, 5), ArgFloat(a, 6), ArgFloat(a, 7), ArgFloat(a, 8),
                 ArgInt(a, 9) != 0, Paint(a, 10));
}

void Canvas_nativeDrawLine(Thread* self, Frame* a, JValue*) {
  DrawState s;
  NativeBitmap* t = Bmp(self, ArgLong(a, 0));
  if (t == nullptr || !State(self, a, 2, &s)) return;
  g_gfx->DrawLine(t, s, ArgFloat(a, 3), ArgFloat(a, 4), ArgFloat(a, 5), ArgFloat(a, 6), Paint(a, 7));
}

void Canvas_nativeDrawPath(Thread* self, Frame* a, JValue*) {
  DrawState s;
  NativeBitmap* t = Bmp(self, ArgLong(a, 0));
  if (t == nullptr || !State(self, a, 2, &s)) return;
  const std::int32_t count = ArgInt(a, 4);
  const float* ops = Floats(self, a, 3, std::max(count, 0));
  if (ops == nullptr || count < 0) return;
  g_gfx->DrawPath(t, s, std::span<const float>(ops, static_cast<std::size_t>(count)), ArgInt(a, 5) != 0, Paint(a, 6));
}

void Canvas_nativeDrawBitmap(Thread* self, Frame* a, JValue*) {
  DrawState s;
  NativeBitmap* t = Bmp(self, ArgLong(a, 0));
  if (t == nullptr || !State(self, a, 2, &s)) return;
  NativeBitmap* src = Bmp(self, ArgLong(a, 3));
  if (src == nullptr) return;
  const float sr[4] = {ArgFloat(a, 5), ArgFloat(a, 6), ArgFloat(a, 7), ArgFloat(a, 8)};
  const float dr[4] = {ArgFloat(a, 9), ArgFloat(a, 10), ArgFloat(a, 11), ArgFloat(a, 12)};
  g_gfx->DrawBitmap(t, s, src, sr, dr, ArgInt(a, 13), ArgInt(a, 14) != 0);
}

void Canvas_nativeDrawText(Thread* self, Frame* a, JValue*) {
  DrawState s;
  NativeBitmap* t = Bmp(self, ArgLong(a, 0));
  if (t == nullptr || !State(self, a, 2, &s)) return;
  const std::u16string text = Rt(self)->GetString(ArgRef(a, 3));
  PaintSpec p;
  p.color = static_cast<std::uint32_t>(ArgInt(a, 10));
  p.flags = ArgInt(a, 11);
  p.shader = Shader(a, 12);
  g_gfx->DrawText(t, s, text, ArgFloat(a, 4), ArgFloat(a, 5), Font(a, 6), p);
}

// --- Paint ------------------------------------------------------------------------------

void Paint_nativeMeasureText(Thread* self, Frame* a, JValue* r) {
  const std::u16string text = Rt(self)->GetString(ArgRef(a, 0));
  r->f = g_gfx->MeasureText(text, Font(a, 1));
}

void Paint_nativeGetTextWidths(Thread* self, Frame* a, JValue*) {
  const std::u16string text = Rt(self)->GetString(ArgRef(a, 0));
  auto* out = static_cast<Array*>(ArgRef(a, 5));
  if (out == nullptr || out->klass->descriptor != "[F" || out->length < static_cast<std::int32_t>(text.size())) {
    Throw(self, "Ljava/lang/ArrayIndexOutOfBoundsException;", "widths array too short");
    return;
  }
  std::vector<float> widths(text.size());
  g_gfx->TextWidths(text, Font(a, 1), widths.data());
  std::memcpy(out->data(), widths.data(), widths.size() * sizeof(float));
}

void Paint_nativeFontMetrics(Thread* self, Frame* a, JValue*) {
  auto* out = static_cast<Array*>(ArgRef(a, 4));
  if (out == nullptr || out->klass->descriptor != "[F" || out->length < 5) {
    Throw(self, "Ljava/lang/IllegalArgumentException;", "font metrics array");
    return;
  }
  const FontMetrics m = g_gfx->Metrics(Font(a, 0));
  float* f = out->elements<float>();
  f[0] = m.top;
  f[1] = m.ascent;
  f[2] = m.descent;
  f[3] = m.bottom;
  f[4] = m.leading;
}

// --- Bitmap -----------------------------------------------------------------------------

void FreeBitmap(void* data) { g_gfx->Free(static_cast<NativeBitmap*>(data)); }

void Bitmap_nativeCreate(Thread* self, Frame* a, JValue* r) {
  std::string error;
  NativeBitmap* b = g_gfx->CreateBitmap(ArgInt(a, 0), ArgInt(a, 1), ArgInt(a, 2) != 0, &error);
  if (b == nullptr) {
    Throw(self, "Ljava/lang/IllegalArgumentException;", error);
    return;
  }
  r->j = Graphics::Handle(b);
}

void Bitmap_nativeCopy(Thread* self, Frame* a, JValue* r) {
  NativeBitmap* src = Bmp(self, ArgLong(a, 0));
  if (src == nullptr) return;
  std::string error;
  NativeBitmap* b = g_gfx->Copy(src, ArgInt(a, 2), ArgInt(a, 3), ArgInt(a, 4), ArgInt(a, 5), ArgInt(a, 6),
                                ArgInt(a, 7), ArgInt(a, 8) != 0, &error);
  if (b == nullptr) {
    Throw(self, "Ljava/lang/IllegalArgumentException;", error);
    return;
  }
  r->j = Graphics::Handle(b);
}

// Bitmap.nativeRegisterCleanup(Bitmap owner, long ptr, long bytes)
void Bitmap_nativeRegisterCleanup(Thread* self, Frame* a, JValue*) {
  Object* owner = ArgRef(a, 0);
  NativeBitmap* b = Bmp(self, ArgLong(a, 1));
  if (b == nullptr) return;
  if (owner == nullptr || g_gfx->Pinned(b)) {
    Throw(self, "Ljava/lang/IllegalArgumentException;", "bitmap cannot be owned");
    return;
  }
  // Ownership moves to the collector exactly once: pin it so no second
  // registration (reflection) can double-free it.
  g_gfx->Pin(b);
  Rt(self)->heap().RegisterNativeAllocation(owner, FreeBitmap, b,
                                            static_cast<std::uint64_t>(std::max<std::int64_t>(0, ArgLong(a, 3))));
}

bool PixelArgs(Thread* self, Frame* a, NativeBitmap** b, Array** pixels) {
  *b = Bmp(self, ArgLong(a, 0));
  if (*b == nullptr) return false;
  *pixels = static_cast<Array*>(ArgRef(a, 2));
  const std::int32_t offset = ArgInt(a, 3), stride = ArgInt(a, 4), w = ArgInt(a, 7), h = ArgInt(a, 8);
  if (*pixels == nullptr || (*pixels)->klass->descriptor != "[I" || w < 0 || h < 0 || offset < 0 || stride < w) {
    Throw(self, "Ljava/lang/IllegalArgumentException;", "pixel arguments");
    return false;
  }
  if (w == 0 || h == 0) return false;
  const std::int64_t last = static_cast<std::int64_t>(offset) + static_cast<std::int64_t>(h - 1) * stride + w;
  if (last > (*pixels)->length) {
    Throw(self, "Ljava/lang/ArrayIndexOutOfBoundsException;", "pixels");
    return false;
  }
  return true;
}

// Bitmap.nativeGetPixels(long ptr, int[] pixels, int offset, int stride, int x, int y, int w, int h)
void Bitmap_nativeGetPixels(Thread* self, Frame* a, JValue*) {
  NativeBitmap* b;
  Array* pixels;
  if (!PixelArgs(self, a, &b, &pixels)) return;
  if (!g_gfx->GetPixels(b, ArgInt(a, 5), ArgInt(a, 6), ArgInt(a, 7), ArgInt(a, 8),
                        pixels->elements<std::uint32_t>() + ArgInt(a, 3), ArgInt(a, 4))) {
    Throw(self, "Ljava/lang/IllegalArgumentException;", "pixel region out of range");
  }
}

void Bitmap_nativeSetPixels(Thread* self, Frame* a, JValue*) {
  NativeBitmap* b;
  Array* pixels;
  if (!PixelArgs(self, a, &b, &pixels)) return;
  if (!g_gfx->SetPixels(b, ArgInt(a, 5), ArgInt(a, 6), ArgInt(a, 7), ArgInt(a, 8),
                        pixels->elements<std::uint32_t>() + ArgInt(a, 3), ArgInt(a, 4))) {
    Throw(self, "Ljava/lang/IllegalArgumentException;", "pixel region out of range");
  }
}

void Bitmap_nativeErase(Thread* self, Frame* a, JValue*) {
  NativeBitmap* b = Bmp(self, ArgLong(a, 0));
  if (b != nullptr) g_gfx->EraseColor(b, static_cast<std::uint32_t>(ArgInt(a, 2)));
}

void Bitmap_nativeHasAlpha(Thread* self, Frame* a, JValue* r) {
  NativeBitmap* b = Bmp(self, ArgLong(a, 0));
  r->i = b != nullptr && g_gfx->HasAlpha(b) ? 1 : 0;
}

void Bitmap_nativeSetHasAlpha(Thread* self, Frame* a, JValue*) {
  NativeBitmap* b = Bmp(self, ArgLong(a, 0));
  if (b != nullptr) g_gfx->SetHasAlpha(b, ArgInt(a, 2) != 0);
}

void Bitmap_nativeGeneration(Thread* self, Frame* a, JValue* r) {
  NativeBitmap* b = Bmp(self, ArgLong(a, 0));
  r->j = b != nullptr ? static_cast<std::int64_t>(g_gfx->Generation(b)) : 0;
}

void Bitmap_nativeCompress(Thread* self, Frame* a, JValue* r) {
  r->l = nullptr;
  NativeBitmap* b = Bmp(self, ArgLong(a, 0));
  if (b == nullptr) return;
  std::string error;
  std::vector<std::uint8_t> data = g_gfx->Encode(b, ArgInt(a, 2), ArgInt(a, 3), &error);
  if (data.empty()) return;
  Runtime* rt = Rt(self);
  Array* arr = rt->heap().AllocArray(self, rt->wk.byte_array, static_cast<std::int32_t>(data.size()));
  if (arr == nullptr) return;
  std::memcpy(arr->data(), data.data(), data.size());
  r->l = arr;
}

// --- BitmapFactory -------------------------------------------------------------------------

std::span<const std::uint8_t> ByteRange(Thread* self, Frame* a, std::uint32_t reg) {
  auto* arr = static_cast<Array*>(ArgRef(a, reg));
  const std::int32_t off = ArgInt(a, reg + 1), len = ArgInt(a, reg + 2);
  if (arr == nullptr || arr->klass->descriptor != "[B" || off < 0 || len < 0 ||
      static_cast<std::int64_t>(off) + len > arr->length) {
    Throw(self, "Ljava/lang/ArrayIndexOutOfBoundsException;", "image data range");
    return {};
  }
  return std::span<const std::uint8_t>(arr->data() + off, static_cast<std::size_t>(len));
}

void BitmapFactory_nativeDecodeBounds(Thread* self, Frame* a, JValue* r) {
  r->i = 0;
  auto bytes = ByteRange(self, a, 0);
  auto* out = static_cast<Array*>(ArgRef(a, 3));
  if (bytes.empty() || out == nullptr || out->klass->descriptor != "[I" || out->length < 2) return;
  int w = 0, h = 0;
  if (!g_gfx->DecodeBounds(bytes, &w, &h)) return;
  out->elements<std::int32_t>()[0] = w;
  out->elements<std::int32_t>()[1] = h;
  r->i = 1;
}

void BitmapFactory_nativeDecode(Thread* self, Frame* a, JValue* r) {
  r->j = 0;
  auto bytes = ByteRange(self, a, 0);
  if (bytes.empty()) return;
  std::string error;
  NativeBitmap* b = g_gfx->Decode(bytes, ArgInt(a, 3), ArgInt(a, 4), &error);
  if (b != nullptr) r->j = Graphics::Handle(b);
}

void DisplayMetrics_nativeDeviceDensity(Thread*, Frame*, JValue* r) { r->i = g_device_density; }

}  // namespace

Status RegisterGraphicsNatives(Runtime* rt, int device_density) {
  std::string error;
  g_gfx = Graphics::Get(&error);
  if (g_gfx == nullptr) return Status(ReasonCode::kHostOsError, "graphics: " + error);
  g_device_density = device_density;
  auto reg = [rt](const char* key, vm::NativeFn fn) { rt->RegisterNative(key, fn); };
  reg("Landroid/graphics/Canvas;.nativeClear(J[FIZ)V", Canvas_nativeClear);
  reg("Landroid/graphics/Canvas;.nativeDrawRect(J[FFFFFFFIIFF[F)V", Canvas_nativeDrawRect);
  reg("Landroid/graphics/Canvas;.nativeDrawOval(J[FFFFFIIFF[F)V", Canvas_nativeDrawOval);
  reg("Landroid/graphics/Canvas;.nativeDrawArc(J[FFFFFFFZIIFF[F)V", Canvas_nativeDrawArc);
  reg("Landroid/graphics/Canvas;.nativeDrawLine(J[FFFFFIIFF[F)V", Canvas_nativeDrawLine);
  reg("Landroid/graphics/Canvas;.nativeDrawPath(J[F[FIZIIFF[F)V", Canvas_nativeDrawPath);
  reg("Landroid/graphics/Canvas;.nativeDrawBitmap(J[FJFFFFFFFFIZ)V", Canvas_nativeDrawBitmap);
  reg("Landroid/graphics/Canvas;.nativeDrawText(J[FLjava/lang/String;FFFIIZII[F)V", Canvas_nativeDrawText);
  reg("Landroid/graphics/Paint;.nativeMeasureText(Ljava/lang/String;FIIZ)F", Paint_nativeMeasureText);
  reg("Landroid/graphics/Paint;.nativeGetTextWidths(Ljava/lang/String;FIIZ[F)V", Paint_nativeGetTextWidths);
  reg("Landroid/graphics/Paint;.nativeFontMetrics(FIIZ[F)V", Paint_nativeFontMetrics);
  reg("Landroid/graphics/Bitmap;.nativeCreate(IIZ)J", Bitmap_nativeCreate);
  reg("Landroid/graphics/Bitmap;.nativeCopy(JIIIIIIZ)J", Bitmap_nativeCopy);
  reg("Landroid/graphics/Bitmap;.nativeRegisterCleanup(Landroid/graphics/Bitmap;JJ)V", Bitmap_nativeRegisterCleanup);
  reg("Landroid/graphics/Bitmap;.nativeGetPixels(J[IIIIIII)V", Bitmap_nativeGetPixels);
  reg("Landroid/graphics/Bitmap;.nativeSetPixels(J[IIIIIII)V", Bitmap_nativeSetPixels);
  reg("Landroid/graphics/Bitmap;.nativeErase(JI)V", Bitmap_nativeErase);
  reg("Landroid/graphics/Bitmap;.nativeHasAlpha(J)Z", Bitmap_nativeHasAlpha);
  reg("Landroid/graphics/Bitmap;.nativeSetHasAlpha(JZ)V", Bitmap_nativeSetHasAlpha);
  reg("Landroid/graphics/Bitmap;.nativeGeneration(J)J", Bitmap_nativeGeneration);
  reg("Landroid/graphics/Bitmap;.nativeCompress(JII)[B", Bitmap_nativeCompress);
  reg("Landroid/graphics/BitmapFactory;.nativeDecodeBounds([BII[I)Z", BitmapFactory_nativeDecodeBounds);
  reg("Landroid/graphics/BitmapFactory;.nativeDecode([BIIII)J", BitmapFactory_nativeDecode);
  reg("Landroid/util/DisplayMetrics;.nativeDeviceDensity()I", DisplayMetrics_nativeDeviceDensity);
  return Status::Ok();
}

}  // namespace arun::fw
