#pragma once

#include "../MMCoding/MMCodingExports.h"

typedef unsigned int GLuint;

namespace NLogging
{
    class ILogger;
};

namespace NMMSS
{
    class ISample;

    class IGLRenderer
    {
    public:
        virtual ~IGLRenderer() {}

        virtual void Render(ISample& sample) = 0;
        virtual void CompleteRender() = 0;
        virtual void Setup(const ISample& sample, GLuint texture, int width, int height, bool mipmaps) = 0;
        virtual bool IsValid() const = 0;
        virtual void GetTextureSize(float& w, float& h) = 0;
        virtual void Destroy() = 0;
    };

    MMCODING_DECLSPEC void InitializeGLRenderer();
    MMCODING_DECLSPEC IGLRenderer* CreateGLRenderer(NLogging::ILogger* ngp_Logger_Ptr_, ISample& sample);
    MMCODING_DECLSPEC void FinalizeDXGLRenderer();
};
