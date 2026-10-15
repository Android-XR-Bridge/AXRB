#include "render.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <format>
#include <numbers>

namespace arun::fw {
namespace {

template <typename T>
class Com {
 public:
  Com() = default;
  Com(const Com& o) : p_(o.p_) {
    if (p_ != nullptr) p_->AddRef();
  }
  Com(Com&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  ~Com() { reset(); }
  Com& operator=(Com o) noexcept {
    std::swap(p_, o.p_);
    return *this;
  }
  T* get() const { return p_; }
  T* operator->() const { return p_; }
  T** put() {
    reset();
    return &p_;
  }
  void reset() {
    if (p_ != nullptr) {
      p_->Release();
      p_ = nullptr;
    }
  }
  explicit operator bool() const { return p_ != nullptr; }

 private:
  T* p_ = nullptr;
};

void EnsureCom() {
  static thread_local bool done = false;
  if (!done) {
    // Multithreaded apartment: graphics objects are used from any thread. A
    // thread that already chose another apartment keeps it.
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    done = true;
  }
}

std::string Hr(const char* what, HRESULT hr) {
  return std::format("{} failed (HRESULT 0x{:08x})", what, static_cast<std::uint32_t>(hr));
}

D2D1_COLOR_F ColorF(std::uint32_t argb, float alpha_scale) {
  return D2D1_COLOR_F{((argb >> 16) & 0xff) / 255.0f, ((argb >> 8) & 0xff) / 255.0f, (argb & 0xff) / 255.0f,
                      ((argb >> 24) & 0xff) / 255.0f * alpha_scale};
}

std::uint32_t FloatBits(float f) {
  std::uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

bool Finite(std::initializer_list<float> values) {
  for (float v : values) {
    if (!std::isfinite(v)) return false;
  }
  return true;
}

}  // namespace

struct Upload {
  ID2D1RenderTarget* target;
  std::uint64_t generation;
  Com<ID2D1Bitmap> bitmap;
};

struct NativeBitmap {
  int width = 0;
  int height = 0;
  bool has_alpha = true;
  bool pinned = false;
  Com<IWICBitmap> wic;
  Com<ID2D1RenderTarget> target;
  Com<ID2D1SolidColorBrush> brush;
  bool drawing = false;
  std::uint64_t generation = 1;
  std::vector<Upload> uploads;  // this bitmap uploaded to other targets
};

struct Graphics::Impl {
  std::mutex lock;
  Com<ID2D1Factory> d2d;
  Com<IDWriteFactory> dw;
  Com<IWICImagingFactory> wic;
  Com<IDWriteFontCollection> fonts;
  std::unordered_set<NativeBitmap*> live;
  std::map<std::tuple<int, int, int, int>, Com<IDWriteTextFormat>> formats;
  std::map<std::tuple<int, int, int>, DWRITE_FONT_METRICS> metrics;
  std::map<std::tuple<int, int, int>, Com<ID2D1StrokeStyle>> strokes;

  static const wchar_t* FamilyName(int family) {
    return family == 1 ? L"Georgia" : family == 2 ? L"Consolas" : L"Segoe UI";
  }

  // --- targets -----------------------------------------------------------------

  bool EnsureTarget(NativeBitmap* b) {
    if (b->target) return true;
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    if (FAILED(d2d->CreateWicBitmapRenderTarget(b->wic.get(), props, b->target.put()))) return false;
    b->target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return SUCCEEDED(b->target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), b->brush.put()));
  }

  // Starts (or continues) a drawing batch on `b`.
  ID2D1RenderTarget* Begin(NativeBitmap* b) {
    if (!EnsureTarget(b)) return nullptr;
    if (!b->drawing) {
      b->target->BeginDraw();
      b->drawing = true;
    }
    ++b->generation;
    return b->target.get();
  }

  // Ends the drawing batch so the pixels can be read or used as a source.
  void Flush(NativeBitmap* b) {
    if (!b->drawing) return;
    b->drawing = false;
    const HRESULT hr = b->target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
      b->target.reset();
      b->brush.reset();
    }
  }

  void DropUploadsFor(ID2D1RenderTarget* target) {
    for (NativeBitmap* other : live) {
      auto& u = other->uploads;
      u.erase(std::remove_if(u.begin(), u.end(), [&](const Upload& x) { return x.target == target; }), u.end());
    }
  }

  ID2D1Bitmap* UploadTo(NativeBitmap* src, ID2D1RenderTarget* target) {
    Flush(src);
    for (Upload& u : src->uploads) {
      if (u.target == target) {
        if (u.generation == src->generation) return u.bitmap.get();
        u.bitmap.reset();
        if (FAILED(target->CreateBitmapFromWicBitmap(src->wic.get(), nullptr, u.bitmap.put()))) return nullptr;
        u.generation = src->generation;
        return u.bitmap.get();
      }
    }
    Upload u{target, src->generation, {}};
    if (FAILED(target->CreateBitmapFromWicBitmap(src->wic.get(), nullptr, u.bitmap.put()))) return nullptr;
    if (src->uploads.size() > 8) src->uploads.erase(src->uploads.begin());
    src->uploads.push_back(std::move(u));
    return src->uploads.back().bitmap.get();
  }

  // --- state ---------------------------------------------------------------------

  // Pushes the clip and transform; false if nothing can be drawn.
  bool Apply(ID2D1RenderTarget* rt, const DrawState& s, bool antialias) {
    if (s.clip[0] >= s.clip[2] || s.clip[1] >= s.clip[3] || s.alpha <= 0) return false;
    if (!Finite({s.m[0], s.m[1], s.m[2], s.m[3], s.m[4], s.m[5]})) return false;
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(s.clip[0]), static_cast<float>(s.clip[1]),
                                        static_cast<float>(s.clip[2]), static_cast<float>(s.clip[3])),
                            D2D1_ANTIALIAS_MODE_ALIASED);
    rt->SetTransform(D2D1::Matrix3x2F(s.m[0], s.m[1], s.m[2], s.m[3], s.m[4], s.m[5]));
    rt->SetAntialiasMode(antialias ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE : D2D1_ANTIALIAS_MODE_ALIASED);
    return true;
  }

  static void Unapply(ID2D1RenderTarget* rt) { rt->PopAxisAlignedClip(); }

  // A brush for `p`: the target's solid brush, or a new gradient brush.
  Com<ID2D1Brush> Brush(NativeBitmap* t, const DrawState& s, const PaintSpec& p) {
    const float paint_alpha = ((p.color >> 24) & 0xff) / 255.0f;
    if (p.shader.size() >= 9) {
      // [kind, tile, n, m0..m5, geometry..., colors(n as int bits), positions(n)]
      const int kind = static_cast<int>(p.shader[0]);
      const int tile = static_cast<int>(p.shader[1]);
      const int n = static_cast<int>(p.shader[2]);
      const std::size_t geom = kind == 1 ? 4 : 3;
      if ((kind == 1 || kind == 2) && n >= 2 && n <= 64 && p.shader.size() >= 9 + geom + 2ull * n) {
        std::vector<D2D1_GRADIENT_STOP> stops(static_cast<std::size_t>(n));
        const float* colors = p.shader.data() + 9 + geom;
        const float* positions = colors + n;
        for (int i = 0; i < n; ++i) {
          stops[i].color = ColorF(FloatBits(colors[i]), 1.0f);
          float pos = positions[i];
          if (!std::isfinite(pos) || pos < 0) pos = static_cast<float>(i) / static_cast<float>(n - 1);
          stops[i].position = std::clamp(pos, 0.0f, 1.0f);
        }
        const D2D1_EXTEND_MODE extend =
            tile == 1 ? D2D1_EXTEND_MODE_WRAP : tile == 2 ? D2D1_EXTEND_MODE_MIRROR : D2D1_EXTEND_MODE_CLAMP;
        Com<ID2D1GradientStopCollection> collection;
        if (SUCCEEDED(t->target->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(n), D2D1_GAMMA_2_2,
                                                              extend, collection.put()))) {
          const float* g = p.shader.data() + 9;
          const D2D1_BRUSH_PROPERTIES bp{paint_alpha * s.alpha,
                                         D2D1::Matrix3x2F(p.shader[3], p.shader[4], p.shader[5], p.shader[6],
                                                          p.shader[7], p.shader[8])};
          if (kind == 1) {
            Com<ID2D1LinearGradientBrush> lg;
            if (SUCCEEDED(t->target->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(g[0], g[1]), D2D1::Point2F(g[2], g[3])), bp,
                    collection.get(), lg.put()))) {
              Com<ID2D1Brush> out;
              lg->QueryInterface(IID_PPV_ARGS(out.put()));
              return out;
            }
          } else {
            Com<ID2D1RadialGradientBrush> rg;
            if (SUCCEEDED(t->target->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(D2D1::Point2F(g[0], g[1]), D2D1::Point2F(0, 0), g[2], g[2]),
                    bp, collection.get(), rg.put()))) {
              Com<ID2D1Brush> out;
              rg->QueryInterface(IID_PPV_ARGS(out.put()));
              return out;
            }
          }
        }
      }
    }
    t->brush->SetColor(ColorF(p.color, s.alpha));
    t->brush->SetOpacity(1.0f);
    Com<ID2D1Brush> out;
    t->brush->QueryInterface(IID_PPV_ARGS(out.put()));
    return out;
  }

  ID2D1StrokeStyle* Stroke(const PaintSpec& p) {
    const int cap = (p.flags >> kPaintCapShift) & 3;
    const int join = (p.flags >> kPaintJoinShift) & 3;
    const int miter = static_cast<int>(std::clamp(p.miter, 1.0f, 100.0f) * 16);
    auto key = std::make_tuple(cap, join, miter);
    auto it = strokes.find(key);
    if (it != strokes.end()) return it->second.get();
    const D2D1_CAP_STYLE c = cap == 1 ? D2D1_CAP_STYLE_ROUND : cap == 2 ? D2D1_CAP_STYLE_SQUARE : D2D1_CAP_STYLE_FLAT;
    const D2D1_LINE_JOIN j =
        join == 1 ? D2D1_LINE_JOIN_ROUND : join == 2 ? D2D1_LINE_JOIN_BEVEL : D2D1_LINE_JOIN_MITER_OR_BEVEL;
    Com<ID2D1StrokeStyle> style;
    d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(c, c, c, j, miter / 16.0f), nullptr, 0, style.put());
    ID2D1StrokeStyle* raw = style.get();
    strokes.emplace(key, std::move(style));
    return raw;
  }

  static float StrokeWidth(const PaintSpec& p) { return p.stroke_width > 0 ? p.stroke_width : 1.0f; }
  static int Style(const PaintSpec& p) { return (p.flags >> kPaintStyleShift) & 3; }

  // Fills and/or strokes `geometry` per the paint style.
  void Paint(NativeBitmap* t, const DrawState& s, ID2D1Geometry* geometry, const PaintSpec& p) {
    ID2D1RenderTarget* rt = t->target.get();
    Com<ID2D1Brush> brush = Brush(t, s, p);
    if (!brush) return;
    const int style = Style(p);
    if (style == 0 || style == 2) rt->FillGeometry(geometry, brush.get());
    if (style == 1 || style == 2) rt->DrawGeometry(geometry, brush.get(), StrokeWidth(p), Stroke(p));
  }

  // --- text ------------------------------------------------------------------------

  IDWriteTextFormat* Format(const FontSpec& f) {
    const int size64 = std::clamp(static_cast<int>(std::lround(f.size * 64)), 64, 2048 * 64);
    const int weight = std::clamp(f.weight, 100, 900);
    auto key = std::make_tuple(f.family, weight, f.italic ? 1 : 0, size64);
    auto it = formats.find(key);
    if (it != formats.end()) return it->second.get();
    Com<IDWriteTextFormat> format;
    if (FAILED(dw->CreateTextFormat(FamilyName(f.family), nullptr, static_cast<DWRITE_FONT_WEIGHT>(weight),
                                    f.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, size64 / 64.0f, L"en-us", format.put()))) {
      return nullptr;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (formats.size() > 512) formats.clear();
    IDWriteTextFormat* raw = format.get();
    formats.emplace(key, std::move(format));
    return raw;
  }

  Com<IDWriteTextLayout> Layout(std::u16string_view text, const FontSpec& f) {
    Com<IDWriteTextLayout> layout;
    IDWriteTextFormat* format = Format(f);
    if (format == nullptr || text.size() > (1u << 20)) return layout;
    dw->CreateTextLayout(reinterpret_cast<const wchar_t*>(text.data()), static_cast<UINT32>(text.size()), format,
                         1e6f, 1e6f, layout.put());
    return layout;
  }

  DWRITE_FONT_METRICS FontMetricsFor(const FontSpec& f) {
    const int weight = std::clamp(f.weight, 100, 900);
    auto key = std::make_tuple(f.family, weight, f.italic ? 1 : 0);
    auto it = metrics.find(key);
    if (it != metrics.end()) return it->second;
    DWRITE_FONT_METRICS m{};
    m.designUnitsPerEm = 2048;
    m.ascent = 2210;
    m.descent = 514;
    UINT32 index = 0;
    BOOL exists = FALSE;
    Com<IDWriteFontFamily> family;
    Com<IDWriteFont> font;
    if (fonts && SUCCEEDED(fonts->FindFamilyName(FamilyName(f.family), &index, &exists)) && exists &&
        SUCCEEDED(fonts->GetFontFamily(index, family.put())) &&
        SUCCEEDED(family->GetFirstMatchingFont(static_cast<DWRITE_FONT_WEIGHT>(weight), DWRITE_FONT_STRETCH_NORMAL,
                                               f.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                               font.put()))) {
      font->GetMetrics(&m);
    }
    metrics.emplace(key, m);
    return m;
  }
};

// --- Graphics ------------------------------------------------------------------------

Graphics* Graphics::Get(std::string* error) {
  static std::mutex init_lock;
  static Graphics* instance = nullptr;
  static std::string init_error;
  std::lock_guard<std::mutex> hold(init_lock);
  EnsureCom();
  if (instance != nullptr) return instance;
  if (!init_error.empty()) {
    *error = init_error;
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  D2D1_FACTORY_OPTIONS options{};
  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory), &options,
                                 reinterpret_cast<void**>(impl->d2d.put()));
  if (FAILED(hr)) {
    init_error = Hr("D2D1CreateFactory", hr);
  } else if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                             reinterpret_cast<IUnknown**>(impl->dw.put())))) {
    init_error = Hr("DWriteCreateFactory", hr);
  } else if (FAILED(hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(impl->wic.put())))) {
    init_error = Hr("CoCreateInstance(WICImagingFactory)", hr);
  } else {
    impl->dw->GetSystemFontCollection(impl->fonts.put(), FALSE);
  }
  if (!init_error.empty()) {
    *error = init_error;
    return nullptr;
  }
  // Deliberately leaked: bitmaps may be freed by the collector at any point
  // until the process exits.
  instance = new Graphics();
  instance->impl_ = std::move(impl);
  return instance;
}

NativeBitmap* Graphics::CreateBitmap(int width, int height, bool has_alpha, std::string* error) {
  EnsureCom();
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    *error = std::format("bitmap size {}x{} out of range", width, height);
    return nullptr;
  }
  std::lock_guard<std::mutex> hold(impl_->lock);
  auto b = std::make_unique<NativeBitmap>();
  HRESULT hr = impl_->wic->CreateBitmap(static_cast<UINT>(width), static_cast<UINT>(height),
                                        GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, b->wic.put());
  if (FAILED(hr)) {
    *error = Hr("IWICImagingFactory::CreateBitmap", hr);
    return nullptr;
  }
  b->width = width;
  b->height = height;
  b->has_alpha = has_alpha;
  NativeBitmap* raw = b.release();
  impl_->live.insert(raw);
  return raw;
}

NativeBitmap* Graphics::Decode(std::span<const std::uint8_t> bytes, int scale_w, int scale_h, std::string* error) {
  EnsureCom();
  if (bytes.empty() || bytes.size() > (1u << 30)) {
    *error = "no image data";
    return nullptr;
  }
  std::lock_guard<std::mutex> hold(impl_->lock);
  IWICImagingFactory* wic = impl_->wic.get();
  Com<IWICStream> stream;
  Com<IWICBitmapDecoder> decoder;
  Com<IWICBitmapFrameDecode> frame;
  HRESULT hr = wic->CreateStream(stream.put());
  if (SUCCEEDED(hr)) {
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()));
  }
  if (SUCCEEDED(hr)) hr = wic->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
  if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, frame.put());
  UINT w = 0, h = 0;
  if (SUCCEEDED(hr)) hr = frame->GetSize(&w, &h);
  if (FAILED(hr) || w == 0 || h == 0 || w > 16384 || h > 16384) {
    *error = FAILED(hr) ? Hr("image decoding", hr) : std::format("image size {}x{} out of range", w, h);
    return nullptr;
  }
  bool has_alpha = true;
  WICPixelFormatGUID format{};
  if (SUCCEEDED(frame->GetPixelFormat(&format))) {
    Com<IWICComponentInfo> info;
    Com<IWICPixelFormatInfo2> pixel;
    BOOL transparency = TRUE;
    if (SUCCEEDED(wic->CreateComponentInfo(format, info.put())) &&
        SUCCEEDED(info->QueryInterface(IID_PPV_ARGS(pixel.put()))) &&
        SUCCEEDED(pixel->SupportsTransparency(&transparency))) {
      has_alpha = transparency != FALSE;
    }
  }
  Com<IWICFormatConverter> converter;
  hr = wic->CreateFormatConverter(converter.put());
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                               WICBitmapPaletteTypeMedianCut);
  }
  IWICBitmapSource* source = converter.get();
  Com<IWICBitmapScaler> scaler;
  if (SUCCEEDED(hr) && scale_w > 0 && scale_h > 0 && (static_cast<UINT>(scale_w) != w || static_cast<UINT>(scale_h) != h)) {
    if (scale_w > 16384 || scale_h > 16384) {
      *error = "scaled image too large";
      return nullptr;
    }
    hr = wic->CreateBitmapScaler(scaler.put());
    if (SUCCEEDED(hr)) {
      hr = scaler->Initialize(converter.get(), static_cast<UINT>(scale_w), static_cast<UINT>(scale_h),
                              WICBitmapInterpolationModeFant);
    }
    source = scaler.get();
    w = static_cast<UINT>(scale_w);
    h = static_cast<UINT>(scale_h);
  }
  auto b = std::make_unique<NativeBitmap>();
  if (SUCCEEDED(hr)) hr = wic->CreateBitmapFromSource(source, WICBitmapCacheOnLoad, b->wic.put());
  if (FAILED(hr)) {
    *error = Hr("image conversion", hr);
    return nullptr;
  }
  b->width = static_cast<int>(w);
  b->height = static_cast<int>(h);
  b->has_alpha = has_alpha;
  NativeBitmap* raw = b.release();
  impl_->live.insert(raw);
  return raw;
}

bool Graphics::DecodeBounds(std::span<const std::uint8_t> bytes, int* width, int* height) {
  EnsureCom();
  if (bytes.empty()) return false;
  std::lock_guard<std::mutex> hold(impl_->lock);
  Com<IWICStream> stream;
  Com<IWICBitmapDecoder> decoder;
  Com<IWICBitmapFrameDecode> frame;
  UINT w = 0, h = 0;
  if (FAILED(impl_->wic->CreateStream(stream.put())) ||
      FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
      FAILED(impl_->wic->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                                 decoder.put())) ||
      FAILED(decoder->GetFrame(0, frame.put())) || FAILED(frame->GetSize(&w, &h))) {
    return false;
  }
  *width = static_cast<int>(w);
  *height = static_cast<int>(h);
  return true;
}

NativeBitmap* Graphics::Copy(NativeBitmap* src, int x, int y, int w, int h, int dst_w, int dst_h, bool filter,
                             std::string* error) {
  EnsureCom();
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > src->width || y + h > src->height || dst_w <= 0 || dst_h <= 0 ||
      dst_w > 16384 || dst_h > 16384) {
    *error = "bitmap region out of range";
    return nullptr;
  }
  std::lock_guard<std::mutex> hold(impl_->lock);
  impl_->Flush(src);
  IWICImagingFactory* wic = impl_->wic.get();
  Com<IWICBitmapClipper> clipper;
  HRESULT hr = wic->CreateBitmapClipper(clipper.put());
  const WICRect rect{x, y, w, h};
  if (SUCCEEDED(hr)) hr = clipper->Initialize(src->wic.get(), &rect);
  IWICBitmapSource* source = clipper.get();
  Com<IWICBitmapScaler> scaler;
  if (SUCCEEDED(hr) && (dst_w != w || dst_h != h)) {
    hr = wic->CreateBitmapScaler(scaler.put());
    if (SUCCEEDED(hr)) {
      hr = scaler->Initialize(clipper.get(), static_cast<UINT>(dst_w), static_cast<UINT>(dst_h),
                              filter ? WICBitmapInterpolationModeFant : WICBitmapInterpolationModeNearestNeighbor);
    }
    source = scaler.get();
  }
  auto b = std::make_unique<NativeBitmap>();
  if (SUCCEEDED(hr)) hr = wic->CreateBitmapFromSource(source, WICBitmapCacheOnLoad, b->wic.put());
  if (FAILED(hr)) {
    *error = Hr("bitmap copy", hr);
    return nullptr;
  }
  b->width = dst_w;
  b->height = dst_h;
  b->has_alpha = src->has_alpha;
  NativeBitmap* raw = b.release();
  impl_->live.insert(raw);
  return raw;
}

void Graphics::Free(NativeBitmap* b) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  if (impl_->live.erase(b) == 0) return;
  if (b->drawing) impl_->Flush(b);
  if (b->target) impl_->DropUploadsFor(b->target.get());
  delete b;
}

NativeBitmap* Graphics::Validate(std::int64_t handle) {
  auto* b = reinterpret_cast<NativeBitmap*>(static_cast<std::intptr_t>(handle));
  std::lock_guard<std::mutex> hold(impl_->lock);
  return impl_->live.count(b) != 0 ? b : nullptr;
}

int Graphics::Width(NativeBitmap* b) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  return b->width;
}

int Graphics::Height(NativeBitmap* b) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  return b->height;
}

bool Graphics::HasAlpha(NativeBitmap* b) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  return b->has_alpha;
}

void Graphics::SetHasAlpha(NativeBitmap* b, bool has_alpha) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  b->has_alpha = has_alpha;
}

std::uint64_t Graphics::Generation(NativeBitmap* b) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  return b->generation;
}

void Graphics::Pin(NativeBitmap* b) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  b->pinned = true;
}

bool Graphics::Pinned(NativeBitmap* b) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  return b->pinned;
}

bool Graphics::Resize(NativeBitmap* b, int width, int height, std::uint32_t argb, std::string* error) {
  EnsureCom();
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    *error = "size out of range";
    return false;
  }
  {
    std::lock_guard<std::mutex> hold(impl_->lock);
    impl_->Flush(b);
    if (b->target) impl_->DropUploadsFor(b->target.get());
    b->target.reset();
    b->brush.reset();
    b->uploads.clear();
    Com<IWICBitmap> fresh;
    const HRESULT hr = impl_->wic->CreateBitmap(static_cast<UINT>(width), static_cast<UINT>(height),
                                                GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, fresh.put());
    if (FAILED(hr)) {
      *error = Hr("IWICImagingFactory::CreateBitmap", hr);
      return false;
    }
    b->wic = fresh;
    b->width = width;
    b->height = height;
    ++b->generation;
  }
  return EraseColor(b, argb);
}

namespace {

// Runs `fn(bytes, stride)` with the bitmap's pixels locked.
template <typename Fn>
bool WithPixels(IWICBitmap* wic, int width, int height, DWORD flags, Fn&& fn) {
  const WICRect all{0, 0, width, height};
  Com<IWICBitmapLock> lock;
  if (FAILED(wic->Lock(&all, flags, lock.put()))) return false;
  UINT stride = 0, size = 0;
  BYTE* data = nullptr;
  if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&size, &data)) || data == nullptr) return false;
  fn(data, stride);
  return true;
}

std::uint32_t Premultiply(std::uint32_t argb) {
  const std::uint32_t a = argb >> 24;
  if (a == 255) return argb;
  if (a == 0) return 0;
  auto ch = [&](int shift) { return ((((argb >> shift) & 0xff) * a + 127) / 255) << shift; };
  return (a << 24) | ch(16) | ch(8) | ch(0);
}

std::uint32_t Unpremultiply(std::uint32_t p) {
  const std::uint32_t a = p >> 24;
  if (a == 255) return p;
  if (a == 0) return 0;
  auto ch = [&](int shift) { return std::min<std::uint32_t>(255, (((p >> shift) & 0xff) * 255 + a / 2) / a) << shift; };
  return (a << 24) | ch(16) | ch(8) | ch(0);
}

}  // namespace

bool Graphics::GetPixels(NativeBitmap* b, int x, int y, int w, int h, std::uint32_t* out, int stride) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > b->width || y + h > b->height) return false;
  impl_->Flush(b);
  return WithPixels(b->wic.get(), b->width, b->height, WICBitmapLockRead, [&](BYTE* data, UINT row_bytes) {
    for (int row = 0; row < h; ++row) {
      const auto* src = reinterpret_cast<const std::uint32_t*>(data + static_cast<std::size_t>(y + row) * row_bytes) + x;
      std::uint32_t* dst = out + static_cast<std::ptrdiff_t>(row) * stride;
      for (int col = 0; col < w; ++col) {
        const std::uint32_t p = Unpremultiply(src[col]);
        dst[col] = b->has_alpha ? p : (p | 0xff000000u);
      }
    }
  });
}

bool Graphics::SetPixels(NativeBitmap* b, int x, int y, int w, int h, const std::uint32_t* in, int stride) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > b->width || y + h > b->height) return false;
  impl_->Flush(b);
  ++b->generation;
  return WithPixels(b->wic.get(), b->width, b->height, WICBitmapLockWrite, [&](BYTE* data, UINT row_bytes) {
    for (int row = 0; row < h; ++row) {
      auto* dst = reinterpret_cast<std::uint32_t*>(data + static_cast<std::size_t>(y + row) * row_bytes) + x;
      const std::uint32_t* src = in + static_cast<std::ptrdiff_t>(row) * stride;
      for (int col = 0; col < w; ++col) {
        dst[col] = Premultiply(b->has_alpha ? src[col] : (src[col] | 0xff000000u));
      }
    }
  });
}

bool Graphics::EraseColor(NativeBitmap* b, std::uint32_t argb) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  impl_->Flush(b);
  ++b->generation;
  const std::uint32_t p = Premultiply(b->has_alpha ? argb : (argb | 0xff000000u));
  return WithPixels(b->wic.get(), b->width, b->height, WICBitmapLockWrite, [&](BYTE* data, UINT row_bytes) {
    for (int row = 0; row < b->height; ++row) {
      auto* dst = reinterpret_cast<std::uint32_t*>(data + static_cast<std::size_t>(row) * row_bytes);
      std::fill(dst, dst + b->width, p);
    }
  });
}

bool Graphics::CopyOpaque(NativeBitmap* b, std::vector<std::uint32_t>* out) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  impl_->Flush(b);
  out->resize(static_cast<std::size_t>(b->width) * static_cast<std::size_t>(b->height));
  return WithPixels(b->wic.get(), b->width, b->height, WICBitmapLockRead, [&](BYTE* data, UINT row_bytes) {
    for (int row = 0; row < b->height; ++row) {
      const auto* src = reinterpret_cast<const std::uint32_t*>(data + static_cast<std::size_t>(row) * row_bytes);
      std::uint32_t* dst = out->data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(b->width);
      for (int col = 0; col < b->width; ++col) dst[col] = src[col] | 0xff000000u;
    }
  });
}

std::vector<std::uint8_t> Graphics::Encode(NativeBitmap* b, int format, int quality, std::string* error) {
  EnsureCom();
  std::lock_guard<std::mutex> hold(impl_->lock);
  impl_->Flush(b);
  IWICImagingFactory* wic = impl_->wic.get();
  Com<IStream> stream;
  Com<IWICBitmapEncoder> encoder;
  Com<IWICBitmapFrameEncode> frame;
  Com<IPropertyBag2> props;
  Com<IWICFormatConverter> converter;
  const bool jpeg = format == 0;
  HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, stream.put());
  if (SUCCEEDED(hr)) hr = wic->CreateEncoder(jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, encoder.put());
  if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
  if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(frame.put(), props.put());
  if (SUCCEEDED(hr) && jpeg && props) {
    PROPBAG2 option{};
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_R4;
    v.fltVal = std::clamp(quality, 0, 100) / 100.0f;
    props->Write(1, &option, &v);
  }
  if (SUCCEEDED(hr)) hr = frame->Initialize(props.get());
  if (SUCCEEDED(hr)) hr = frame->SetSize(static_cast<UINT>(b->width), static_cast<UINT>(b->height));
  WICPixelFormatGUID pf = jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
  if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&pf);
  if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(converter.put());
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(b->wic.get(), pf, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
  }
  if (SUCCEEDED(hr)) hr = frame->WriteSource(converter.get(), nullptr);
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();
  std::vector<std::uint8_t> out;
  HGLOBAL global = nullptr;
  if (SUCCEEDED(hr)) hr = GetHGlobalFromStream(stream.get(), &global);
  if (SUCCEEDED(hr)) {
    STATSTG stat{};
    stream->Stat(&stat, STATFLAG_NONAME);
    const void* data = GlobalLock(global);
    if (data != nullptr) {
      const auto n = static_cast<std::size_t>(stat.cbSize.QuadPart);
      out.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + n);
      GlobalUnlock(global);
    }
  }
  if (FAILED(hr)) *error = Hr("image encoding", hr);
  return out;
}

std::string Graphics::WritePng(NativeBitmap* b, const std::filesystem::path& path) {
  std::string error;
  std::vector<std::uint8_t> png = Encode(b, 1, 100, &error);
  if (png.empty()) return error.empty() ? "PNG encoding failed" : error;
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return std::format("cannot create the file ({})", GetLastError());
  DWORD written = 0;
  const BOOL ok = WriteFile(f, png.data(), static_cast<DWORD>(png.size()), &written, nullptr);
  CloseHandle(f);
  if (!ok || written != png.size()) return "cannot write the file";
  return {};
}

// --- drawing ------------------------------------------------------------------------------

void Graphics::Clear(NativeBitmap* t, const DrawState& s, std::uint32_t argb, bool src_mode) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  ID2D1RenderTarget* rt = impl_->Begin(t);
  if (rt == nullptr || s.clip[0] >= s.clip[2] || s.clip[1] >= s.clip[3]) return;
  rt->SetTransform(D2D1::Matrix3x2F::Identity());
  const D2D1_RECT_F clip = D2D1::RectF(static_cast<float>(s.clip[0]), static_cast<float>(s.clip[1]),
                                       static_cast<float>(s.clip[2]), static_cast<float>(s.clip[3]));
  rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
  if (src_mode) {
    rt->Clear(ColorF(argb, s.alpha));
  } else {
    t->brush->SetColor(ColorF(argb, s.alpha));
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    rt->FillRectangle(clip, t->brush.get());
  }
  rt->PopAxisAlignedClip();
}

void Graphics::DrawRect(NativeBitmap* t, const DrawState& s, float l, float top, float r, float b, float rx, float ry,
                        const PaintSpec& p) {
  if (!Finite({l, top, r, b, rx, ry})) return;
  std::lock_guard<std::mutex> hold(impl_->lock);
  ID2D1RenderTarget* rt = impl_->Begin(t);
  if (rt == nullptr || !impl_->Apply(rt, s, (p.flags & kPaintAntiAlias) != 0)) return;
  Com<ID2D1Brush> brush = impl_->Brush(t, s, p);
  if (brush) {
    const D2D1_RECT_F rect = D2D1::RectF(l, top, r, b);
    const int style = Impl::Style(p);
    if (rx > 0 && ry > 0) {
      const D2D1_ROUNDED_RECT rr{rect, rx, ry};
      if (style == 0 || style == 2) rt->FillRoundedRectangle(rr, brush.get());
      if (style == 1 || style == 2) rt->DrawRoundedRectangle(rr, brush.get(), Impl::StrokeWidth(p), impl_->Stroke(p));
    } else {
      if (style == 0 || style == 2) rt->FillRectangle(rect, brush.get());
      if (style == 1 || style == 2) rt->DrawRectangle(rect, brush.get(), Impl::StrokeWidth(p), impl_->Stroke(p));
    }
  }
  Impl::Unapply(rt);
}

void Graphics::DrawOval(NativeBitmap* t, const DrawState& s, float l, float top, float r, float b, const PaintSpec& p) {
  if (!Finite({l, top, r, b})) return;
  std::lock_guard<std::mutex> hold(impl_->lock);
  ID2D1RenderTarget* rt = impl_->Begin(t);
  if (rt == nullptr || !impl_->Apply(rt, s, (p.flags & kPaintAntiAlias) != 0)) return;
  Com<ID2D1Brush> brush = impl_->Brush(t, s, p);
  if (brush) {
    const D2D1_ELLIPSE e{D2D1::Point2F((l + r) / 2, (top + b) / 2), (r - l) / 2, (b - top) / 2};
    const int style = Impl::Style(p);
    if (style == 0 || style == 2) rt->FillEllipse(e, brush.get());
    if (style == 1 || style == 2) rt->DrawEllipse(e, brush.get(), Impl::StrokeWidth(p), impl_->Stroke(p));
  }
  Impl::Unapply(rt);
}

void Graphics::DrawArc(NativeBitmap* t, const DrawState& s, float l, float top, float r, float b, float start,
                       float sweep, bool use_center, const PaintSpec& p) {
  if (!Finite({l, top, r, b, start, sweep}) || sweep == 0) return;
  if (sweep >= 360 || sweep <= -360) {
    if (!use_center) {
      DrawOval(t, s, l, top, r, b, p);
      return;
    }
    sweep = sweep > 0 ? 359.99f : -359.99f;
  }
  std::lock_guard<std::mutex> hold(impl_->lock);
  ID2D1RenderTarget* rt = impl_->Begin(t);
  if (rt == nullptr || !impl_->Apply(rt, s, (p.flags & kPaintAntiAlias) != 0)) return;
  const float cx = (l + r) / 2, cy = (top + b) / 2, rx = (r - l) / 2, ry = (b - top) / 2;
  auto point = [&](float deg) {
    const float rad = deg * std::numbers::pi_v<float> / 180.0f;
    return D2D1::Point2F(cx + rx * std::cos(rad), cy + ry * std::sin(rad));
  };
  Com<ID2D1PathGeometry> geometry;
  Com<ID2D1GeometrySink> sink;
  if (SUCCEEDED(impl_->d2d->CreatePathGeometry(geometry.put())) && SUCCEEDED(geometry->Open(sink.put()))) {
    if (use_center) {
      sink->BeginFigure(D2D1::Point2F(cx, cy), D2D1_FIGURE_BEGIN_FILLED);
      sink->AddLine(point(start));
    } else {
      sink->BeginFigure(point(start), D2D1_FIGURE_BEGIN_FILLED);
    }
    sink->AddArc(D2D1::ArcSegment(point(start + sweep), D2D1::SizeF(rx, ry), 0,
                                  sweep > 0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                  std::fabs(sweep) > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(use_center ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    if (SUCCEEDED(sink->Close())) impl_->Paint(t, s, geometry.get(), p);
  }
  Impl::Unapply(rt);
}

void Graphics::DrawLine(NativeBitmap* t, const DrawState& s, float x0, float y0, float x1, float y1,
                        const PaintSpec& p) {
  if (!Finite({x0, y0, x1, y1})) return;
  std::lock_guard<std::mutex> hold(impl_->lock);
  ID2D1RenderTarget* rt = impl_->Begin(t);
  if (rt == nullptr || !impl_->Apply(rt, s, (p.flags & kPaintAntiAlias) != 0)) return;
  Com<ID2D1Brush> brush = impl_->Brush(t, s, p);
  if (brush) rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush.get(), Impl::StrokeWidth(p), impl_->Stroke(p));
  Impl::Unapply(rt);
}

void Graphics::DrawPath(NativeBitmap* t, const DrawState& s, std::span<const float> ops, bool even_odd,
                        const PaintSpec& p) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  ID2D1RenderTarget* rt = impl_->Begin(t);
  if (rt == nullptr || !impl_->Apply(rt, s, (p.flags & kPaintAntiAlias) != 0)) return;
  Com<ID2D1PathGeometry> geometry;
  Com<ID2D1GeometrySink> sink;
  if (SUCCEEDED(impl_->d2d->CreatePathGeometry(geometry.put())) && SUCCEEDED(geometry->Open(sink.put()))) {
    sink->SetFillMode(even_odd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
    bool open = false;
    D2D1_POINT_2F last{0, 0};
    std::size_t i = 0;
    bool bad = false;
    auto need = [&](std::size_t n) {
      if (i + n > ops.size()) bad = true;
      return !bad;
    };
    auto ensure_open = [&]() {
      if (!open) {
        sink->BeginFigure(last, D2D1_FIGURE_BEGIN_FILLED);
        open = true;
      }
    };
    while (i < ops.size() && !bad) {
      const int verb = static_cast<int>(ops[i++]);
      switch (verb) {
        case 0:
          if (!need(2)) break;
          if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
          open = false;
          last = D2D1::Point2F(ops[i], ops[i + 1]);
          i += 2;
          break;
        case 1:
          if (!need(2)) break;
          ensure_open();
          last = D2D1::Point2F(ops[i], ops[i + 1]);
          sink->AddLine(last);
          i += 2;
          break;
        case 2:
          if (!need(4)) break;
          ensure_open();
          sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(ops[i], ops[i + 1]),
                                                                D2D1::Point2F(ops[i + 2], ops[i + 3])));
          last = D2D1::Point2F(ops[i + 2], ops[i + 3]);
          i += 4;
          break;
        case 3:
          if (!need(6)) break;
          ensure_open();
          sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(ops[i], ops[i + 1]), D2D1::Point2F(ops[i + 2], ops[i + 3]),
                                              D2D1::Point2F(ops[i + 4], ops[i + 5])));
          last = D2D1::Point2F(ops[i + 4], ops[i + 5]);
          i += 6;
          break;
        case 4:
          if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
          open = false;
          break;
        default:
          bad = true;
          break;
      }
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (SUCCEEDED(sink->Close()) && !bad) impl_->Paint(t, s, geometry.get(), p);
  }
  Impl::Unapply(rt);
}

void Graphics::DrawBitmap(NativeBitmap* t, const DrawState& s, NativeBitmap* src, const float srcr[4],
                          const float dst[4], int alpha, bool filter) {
  if (src == t || !Finite({srcr[0], srcr[1], srcr[2], srcr[3], dst[0], dst[1], dst[2], dst[3]})) return;
  std::lock_guard<std::mutex> hold(impl_->lock);
  ID2D1RenderTarget* rt = impl_->Begin(t);
  if (rt == nullptr) return;
  ID2D1Bitmap* bmp = impl_->UploadTo(src, rt);
  if (bmp == nullptr || !impl_->Apply(rt, s, true)) return;
  const D2D1_RECT_F s_rect = D2D1::RectF(srcr[0], srcr[1], srcr[2], srcr[3]);
  rt->DrawBitmap(bmp, D2D1::RectF(dst[0], dst[1], dst[2], dst[3]), std::clamp(alpha, 0, 255) / 255.0f * s.alpha,
                 filter ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                 &s_rect);
  Impl::Unapply(rt);
}

void Graphics::DrawText(NativeBitmap* t, const DrawState& s, std::u16string_view text, float x, float y,
                        const FontSpec& f, const PaintSpec& p) {
  if (text.empty() || !Finite({x, y, f.size})) return;
  std::lock_guard<std::mutex> hold(impl_->lock);
  ID2D1RenderTarget* rt = impl_->Begin(t);
  if (rt == nullptr) return;
  Com<IDWriteTextLayout> layout = impl_->Layout(text, f);
  if (!layout || !impl_->Apply(rt, s, true)) return;
  const DWRITE_TEXT_RANGE all{0, static_cast<UINT32>(text.size())};
  if (p.flags & kPaintUnderline) layout->SetUnderline(TRUE, all);
  if (p.flags & kPaintStrikeThru) layout->SetStrikethrough(TRUE, all);
  DWRITE_LINE_METRICS line{};
  UINT32 lines = 0;
  layout->GetLineMetrics(&line, 1, &lines);
  Com<ID2D1Brush> brush = impl_->Brush(t, s, p);
  if (brush) rt->DrawTextLayout(D2D1::Point2F(x, y - line.baseline), layout.get(), brush.get());
  Impl::Unapply(rt);
}

// --- text -----------------------------------------------------------------------------------

float Graphics::MeasureText(std::u16string_view text, const FontSpec& f) {
  if (text.empty()) return 0;
  std::lock_guard<std::mutex> hold(impl_->lock);
  Com<IDWriteTextLayout> layout = impl_->Layout(text, f);
  if (!layout) return 0;
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  return m.widthIncludingTrailingWhitespace;
}

void Graphics::TextWidths(std::u16string_view text, const FontSpec& f, float* out) {
  std::fill(out, out + text.size(), 0.0f);
  if (text.empty()) return;
  std::lock_guard<std::mutex> hold(impl_->lock);
  Com<IDWriteTextLayout> layout = impl_->Layout(text, f);
  if (!layout) return;
  UINT32 count = 0;
  layout->GetClusterMetrics(nullptr, 0, &count);
  std::vector<DWRITE_CLUSTER_METRICS> clusters(count);
  if (count == 0 || FAILED(layout->GetClusterMetrics(clusters.data(), count, &count))) return;
  std::size_t pos = 0;
  for (const DWRITE_CLUSTER_METRICS& c : clusters) {
    if (pos >= text.size()) break;
    out[pos] = c.width;
    pos += c.length;
  }
}

FontMetrics Graphics::Metrics(const FontSpec& f) {
  std::lock_guard<std::mutex> hold(impl_->lock);
  const DWRITE_FONT_METRICS m = impl_->FontMetricsFor(f);
  const float scale = f.size / static_cast<float>(m.designUnitsPerEm != 0 ? m.designUnitsPerEm : 2048);
  FontMetrics out{};
  out.ascent = -static_cast<float>(m.ascent) * scale;
  out.descent = static_cast<float>(m.descent) * scale;
  out.top = out.ascent;
  out.bottom = out.descent;
  out.leading = static_cast<float>(m.lineGap) * scale;
  return out;
}

}  // namespace arun::fw
