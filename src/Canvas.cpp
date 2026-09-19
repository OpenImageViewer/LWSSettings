#include "Canvas.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#ifdef _WIN32
    #include <windows.h>
    #include <d2d1.h>
    #include <dwrite.h>
    #include <wincodec.h>
    #include <wrl/client.h>
#else
    #include <pango/pangocairo.h>
    #include <fontconfig/fontconfig.h>
#endif

namespace LWSSettings
{
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    static void Check(HRESULT hr)
    {
        if (FAILED(hr))
            throw std::runtime_error("Cannot create settings canvas");
    }
    static std::wstring Wide(std::string_view text)
    {
        if (text.empty())
            return {};
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
        std::wstring s(n, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), s.data(), n);
        return s;
    }
    struct Canvas::Impl
    {
        int width = 0, height = 0;
        ComPtr<ID2D1Factory> factory;
        ComPtr<IDWriteFactory> fonts;
        ComPtr<IWICImagingFactory> imaging;
        ComPtr<IWICBitmap> bitmap;
        ComPtr<ID2D1RenderTarget> target;
        ComPtr<IWICBitmapLock> lock;
        bool com = false;
        Impl()
        {
            auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            com = SUCCEEDED(hr);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
                Check(hr);
            Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
            Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                      reinterpret_cast<IUnknown**>(fonts.GetAddressOf())));
            Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imaging)));
        }
        ~Impl()
        {
            lock.Reset();
            target.Reset();
            bitmap.Reset();
            imaging.Reset();
            fonts.Reset();
            factory.Reset();
            if (com)
                CoUninitialize();
        }
        ComPtr<IDWriteTextLayout> Layout(std::string_view text, float width, float height, float size, bool wrap)
        {
            ComPtr<IDWriteTextFormat> format;
            Check(fonts->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, size, L"", &format));
            format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
            auto wide = Wide(text);
            ComPtr<IDWriteTextLayout> layout;
            Check(fonts->CreateTextLayout(wide.data(), UINT32(wide.size()), format.Get(), std::max(1.f, width),
                                          std::max(1.f, height), &layout));
            return layout;
        }
    };
#else
    struct Canvas::Impl
    {
        int width = 0, height = 0;
        Impl()
        {
            if (!FcInit())
                throw std::runtime_error("Cannot initialize system fonts");
        }
        std::vector<std::byte> pixels;
        cairo_surface_t* surface = nullptr;
        cairo_t* context = nullptr;
        ~Impl()
        {
            if (context)
                cairo_destroy(context);
            if (surface)
                cairo_surface_destroy(surface);
        }
        PangoLayout* Layout(std::string_view text, float width, float size, bool wrap)
        {
            PangoLayout* layout = pango_cairo_create_layout(context);
            pango_layout_set_text(layout, text.data(), int(text.size()));
            auto* font = pango_font_description_from_string("sans-serif");
            pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
            pango_layout_set_font_description(layout, font);
            pango_font_description_free(font);
            if (wrap)
            {
                pango_layout_set_width(layout, int(width * PANGO_SCALE));
                pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
            }
            return layout;
        }
    };
#endif
    Canvas::Canvas() : impl_(std::make_unique<Impl>()) {}
    Canvas::~Canvas() = default;
    void Canvas::Begin(int width, int height)
    {
        auto& c = *impl_;
        width = std::max(1, width);
        height = std::max(1, height);
#ifdef _WIN32
        c.lock.Reset();
        if (width != c.width || height != c.height)
        {
            c.target.Reset();
            c.bitmap.Reset();
            Check(
                c.imaging->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &c.bitmap));
            auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            Check(c.factory->CreateWicBitmapRenderTarget(c.bitmap.Get(), properties, &c.target));
        }
        c.target->BeginDraw();
#else
        if (width != c.width || height != c.height)
        {
            if (c.context)
                cairo_destroy(c.context);
            if (c.surface)
                cairo_surface_destroy(c.surface);
            c.pixels.resize(size_t(width) * size_t(height) * 4);
            c.surface = cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(c.pixels.data()),
                                                            CAIRO_FORMAT_ARGB32, width, height, width * 4);
            c.context = cairo_create(c.surface);
            if (cairo_status(c.context) != CAIRO_STATUS_SUCCESS)
                throw std::runtime_error("Cannot create settings canvas");
        }
#endif
        c.width = width;
        c.height = height;
    }
    void Canvas::Fill(float x, float y, float width, float height, LLUtils::Color color)
    {
        if (width <= 0 || height <= 0)
            return;
#ifdef _WIN32
        ComPtr<ID2D1SolidColorBrush> brush;
        Check(impl_->target->CreateSolidColorBrush(
            D2D1::ColorF(color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f), &brush));
        impl_->target->FillRectangle(D2D1::RectF(x, y, x + width, y + height), brush.Get());
#else
        auto* c = impl_->context;
        cairo_set_source_rgba(c, color.R() / 255., color.G() / 255., color.B() / 255., color.A() / 255.);
        cairo_rectangle(c, x, y, width, height);
        cairo_fill(c);
#endif
    }
    void Canvas::Text(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                      float size, bool wrap)
    {
        if (text.empty() || width <= 0 || height <= 0)
            return;
        Clip(x, y, width, height);
#ifdef _WIN32
        ComPtr<ID2D1SolidColorBrush> brush;
        Check(impl_->target->CreateSolidColorBrush(
            D2D1::ColorF(color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f), &brush));
        auto layout = impl_->Layout(text, width, height, size, wrap);
        impl_->target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush.Get());
#else
        auto* layout = impl_->Layout(text, width, size, wrap);
        cairo_set_source_rgba(impl_->context, color.R() / 255., color.G() / 255., color.B() / 255., color.A() / 255.);
        cairo_move_to(impl_->context, x, y);
        pango_cairo_show_layout(impl_->context, layout);
        g_object_unref(layout);
#endif
        Unclip();
    }
    float Canvas::Measure(std::string_view text, float size)
    {
#ifdef _WIN32
        auto layout = impl_->Layout(text, 100000, size * 2, size, false);
        DWRITE_TEXT_METRICS metrics{};
        Check(layout->GetMetrics(&metrics));
        return metrics.widthIncludingTrailingWhitespace;
#else
        auto* layout = impl_->Layout(text, 100000, size, false);
        int width = 0;
        pango_layout_get_pixel_size(layout, &width, nullptr);
        g_object_unref(layout);
        return float(width);
#endif
    }
    float Canvas::TextHeight(std::string_view text, float width, float size)
    {
#ifdef _WIN32
        auto layout = impl_->Layout(text, width, 100000, size, true);
        DWRITE_TEXT_METRICS metrics{};
        Check(layout->GetMetrics(&metrics));
        return metrics.height;
#else
        auto* layout = impl_->Layout(text, width, size, true);
        int height = 0;
        pango_layout_get_pixel_size(layout, nullptr, &height);
        g_object_unref(layout);
        return float(height);
#endif
    }
    void Canvas::Clip(float x, float y, float width, float height)
    {
#ifdef _WIN32
        impl_->target->PushAxisAlignedClip(D2D1::RectF(x, y, x + std::max(0.f, width), y + std::max(0.f, height)),
                                           D2D1_ANTIALIAS_MODE_ALIASED);
#else
        cairo_save(impl_->context);
        cairo_rectangle(impl_->context, x, y, std::max(0.f, width), std::max(0.f, height));
        cairo_clip(impl_->context);
#endif
    }
    void Canvas::Unclip()
    {
#ifdef _WIN32
        impl_->target->PopAxisAlignedClip();
#else
        cairo_restore(impl_->context);
#endif
    }
    LWS::BitmapBuffer Canvas::End()
    {
        auto& c = *impl_;
#ifdef _WIN32
        Check(c.target->EndDraw());
        WICRect rect{0, 0, c.width, c.height};
        Check(c.bitmap->Lock(&rect, WICBitmapLockRead, &c.lock));
        UINT size = 0, stride = 0;
        BYTE* data = nullptr;
        Check(c.lock->GetDataPointer(&size, &data));
        Check(c.lock->GetStride(&stride));
        buffer_ = {{reinterpret_cast<const std::byte*>(data), size},
                   LWS::BitmapPixelFormat::Bgra8Premultiplied,
                   LWS::BitmapRowOrder::TopDown,
                   uint32_t(c.width),
                   uint32_t(c.height),
                   stride};
#else
        cairo_surface_flush(c.surface);
        buffer_ = {c.pixels,
                   LWS::BitmapPixelFormat::Bgra8Premultiplied,
                   LWS::BitmapRowOrder::TopDown,
                   uint32_t(c.width),
                   uint32_t(c.height),
                   uint32_t(c.width * 4)};
#endif
        return buffer_;
    }
}  // namespace LWSSettings
