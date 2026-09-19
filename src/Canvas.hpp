#pragma once
#include <LWS/Window.hpp>
#include <memory>
#include <string_view>
#include <vector>
namespace LWSSettings
{
    class Canvas
    {
      public:

        Canvas();
        ~Canvas();
        void Begin(int width, int height);
        void Fill(float x, float y, float width, float height, LLUtils::Color color);
        void Text(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                  float size = 14, bool wrap = false);
        float Measure(std::string_view text, float size = 14);
        float TextHeight(std::string_view text, float width, float size = 14);
        void Clip(float x, float y, float width, float height);
        void Unclip();
        LWS::BitmapBuffer End();
        LWS::BitmapBuffer Pixels() const { return buffer_; }

      private:

        struct Impl;
        std::unique_ptr<Impl> impl_;
        LWS::BitmapBuffer buffer_;
    };
}  // namespace LWSSettings
