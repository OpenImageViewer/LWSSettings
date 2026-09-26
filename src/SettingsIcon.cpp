#include "SettingsIcon.hpp"
#include <algorithm>
#include <cmath>
#ifdef _WIN32
    #include <windows.h>
    #include <wincodec.h>
    #include <wrl/client.h>
#else
    #include <cairo/cairo.h>
#endif
namespace LWSSettings::internal
{
    LWS::BitmapSharedPtr DefaultSettingsIcon()
    {
        // Embedded vector geometry: broad teeth and open hubs remain legible at small sizes.
        static auto icon = []
        {
            constexpr unsigned side = 128, samples = 4;
            std::vector<std::byte> pixels(side * side * 4);
            auto gear = [](float x, float y, float cx, float cy, float radius, unsigned teeth)
            {
                x -= cx;
                y -= cy;
                const float distance = std::sqrt(x * x + y * y) / radius;
                const float angle = std::atan2(y, x);
                const float edge = std::cos(angle * teeth) > -.2f ? 1.f : .79f;
                return distance > .34f && distance < edge;
            };
            for (unsigned y = 0; y < side; ++y)
                for (unsigned x = 0; x < side; ++x)
                {
                    unsigned r = 0, g = 0, b = 0, a = 0;
                    for (unsigned sy = 0; sy < samples; ++sy)
                        for (unsigned sx = 0; sx < samples; ++sx)
                        {
                            float px = x + (sx + .5f) / samples, py = y + (sy + .5f) / samples;
                            if (gear(px, py, 85, 86, 31, 8))
                            {
                                r += 242;
                                g += 183;
                                b += 74;
                                a += 255;
                            }
                            else if (gear(px, py, 47, 45, 38, 10))
                            {
                                r += 108;
                                g += 174;
                                b += 239;
                                a += 255;
                            }
                        }
                    auto* p = pixels.data() + (y * side + x) * 4;
                    p[0] = std::byte(b / 16);
                    p[1] = std::byte(g / 16);
                    p[2] = std::byte(r / 16);
                    p[3] = std::byte(a / 16);
                }
            return std::make_shared<LWS::Bitmap>(LWS::BitmapBuffer{pixels, LWS::BitmapPixelFormat::Bgra8Premultiplied,
                                                                   LWS::BitmapRowOrder::TopDown, side, side, side * 4});
        }();
        return icon;
    }
    std::expected<LWS::BitmapSharedPtr, Error> LoadSettingsIcon(const std::filesystem::path& path)
    {
        if (path.empty())
            return DefaultSettingsIcon();
        auto failure = [&]
        { return std::unexpected(Error{"icon", "Cannot load settings icon PNG: " + path.string()}); };
        try
        {
            unsigned width = 0, height = 0, pitch = 0;
            std::vector<std::byte> pixels;
#ifdef _WIN32
            using Microsoft::WRL::ComPtr;
            const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            struct Apartment
            {
                bool owns;
                ~Apartment()
                {
                    if (owns)
                        CoUninitialize();
                }
            } apartment{SUCCEEDED(hr)};
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
                return failure();
            ComPtr<IWICImagingFactory> factory;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> converter;
            if (FAILED(
                    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
                FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                          WICDecodeMetadataCacheOnDemand, &decoder)))
                return failure();
            GUID format{};
            if (FAILED(decoder->GetContainerFormat(&format)) || format != GUID_ContainerFormatPng ||
                FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) || !width || !height ||
                width > 2048 || height > 2048)
                return failure();
            pitch = width * 4;
            pixels.resize(size_t(pitch) * height);
            if (FAILED(factory->CreateFormatConverter(&converter)) ||
                FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                             nullptr, 0, WICBitmapPaletteTypeCustom)) ||
                FAILED(
                    converter->CopyPixels(nullptr, pitch, UINT(pixels.size()), reinterpret_cast<BYTE*>(pixels.data()))))
                return failure();
#else
            std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> surface(
                cairo_image_surface_create_from_png(path.c_str()), cairo_surface_destroy);
            if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS)
                return failure();
            width = cairo_image_surface_get_width(surface.get());
            height = cairo_image_surface_get_height(surface.get());
            if (!width || !height || width > 2048 || height > 2048)
                return failure();
            // Paint onto ARGB32 also normalizes PNGs decoded as opaque RGB24 or A8.
            std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> argb(
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height), cairo_surface_destroy);
            std::unique_ptr<cairo_t, decltype(&cairo_destroy)> context(cairo_create(argb.get()), cairo_destroy);
            cairo_set_source_surface(context.get(), surface.get(), 0, 0);
            cairo_paint(context.get());
            cairo_surface_flush(argb.get());
            if (cairo_surface_status(argb.get()) != CAIRO_STATUS_SUCCESS)
                return failure();
            pitch = cairo_image_surface_get_stride(argb.get());
            auto* data = reinterpret_cast<std::byte*>(cairo_image_surface_get_data(argb.get()));
            pixels.assign(data, data + size_t(pitch) * height);
#endif
            // One square, immutable source is shared by native icons and the header.
            const unsigned side = std::max(width, height);
            std::vector<std::byte> square(size_t(side) * side * 4);
            for (unsigned y = 0; y < height; ++y)
                std::copy_n(pixels.data() + size_t(y) * pitch, width * 4,
                            square.data() + (size_t(y + (side - height) / 2) * side + (side - width) / 2) * 4);
            return std::make_shared<LWS::Bitmap>(LWS::BitmapBuffer{square, LWS::BitmapPixelFormat::Bgra8Premultiplied,
                                                                   LWS::BitmapRowOrder::TopDown, side, side, side * 4});
        }
        catch (const std::exception&)
        {
            return failure();
        }
    }
}  // namespace LWSSettings::internal
