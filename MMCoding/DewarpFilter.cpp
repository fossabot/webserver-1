#include <glm/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Transforms.h"
#include "../FilterImpl.h"

namespace
{
    // http://www.paulbourke.net/dome/dualfish2sphere/
    
    class CFisheyeDewarpTransformer
    {
        DECLARE_LOGGER_HOLDER;

    public:

        CFisheyeDewarpTransformer(DECLARE_LOGGER_ARG, float fisheyeLeft, float fisheyeRight, float fisheyeTop, float fisheyeBottom, float fov, float pan, float tilt, float zoom, uint32_t destWidth, uint32_t destHeight, NMMSS::ECameraPlace place)
            : m_fisheyeLeft(fisheyeLeft)
            , m_fisheyeRight(fisheyeRight)
            , m_fisheyeTop(fisheyeTop)
            , m_fisheyeBottom(fisheyeBottom)
            , m_fov(fov)
            , m_pan(pan)
            , m_tilt(tilt)
            , m_zoom(zoom)
            , m_place(place)
            , m_destWidth((destWidth + 7) & -8)
            , m_destHeight((destHeight + 7) & -8)
            , m_srcWidth(0)
            , m_srcHeight(0)
            , m_initialized(false)
        {
            INIT_LOGGER_HOLDER;

            if (m_zoom > m_fov || m_zoom <= 0.0f)
                throw std::runtime_error("bad 'viewAngle' parameter");

            if(m_destWidth == 0 || m_destHeight == 0)
                throw std::runtime_error("bad 'destWidth' or 'destHeight' parameter");
        }

        NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            m_allocator = holder.GetAllocator();

            try
            {
                NMMSS::NMediaType::ApplyMediaTypeVisitor(sample, *this);

                if (NMMSS::ETRANSFORMED == m_result)
                {
                    m_dewarpSample->Header().dtTimeBegin = sample->Header().dtTimeBegin;
                    m_dewarpSample->Header().dtTimeEnd = sample->Header().dtTimeEnd;
                    holder.AddSample(m_dewarpSample);
                }
            }
            catch (std::exception& e)
            {
                _log_ << e.what() << std::endl;
                return NMMSS::EFAILED;
            }
            return m_result;
        }

        template<typename TMediaTypeHeader>
        void operator()(TMediaTypeHeader* header, uint8_t* body)
        {
            m_result = NMMSS::ETHROUGH;
        }

        void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header, uint8_t* body)
        {
            Init(header->nWidth, header->nHeight);

            uint32_t dstPitch = (m_destWidth + 15) & -16;
            uint32_t bodySize = dstPitch * m_destHeight;
            m_dewarpSample = m_allocator->Alloc(bodySize);
            m_dewarpSample->Header().nBodySize = bodySize;

            uint8_t* dewarpBody = m_dewarpSample->GetBody();
            NMMSS::NMediaType::Video::fccGREY::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(m_dewarpSample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader));

            subheader->nOffset = 0;
            subheader->nWidth = m_destWidth;
            subheader->nHeight = m_destHeight;
            subheader->nPitch = dstPitch;

            memset(dewarpBody, 0, dstPitch * m_destHeight);

            MakeDewarp(body, 0, 0, header->nPitch, 0, 0, dewarpBody, 0, 0, subheader->nPitch, 0, 0, 0, 0);

            m_result = NMMSS::ETRANSFORMED;
        }

        void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* body)
        {
            DoTransform<NMMSS::NMediaType::Video::fccI420>(header, body, 2, 2);
        }

        void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* body)
        {
            DoTransform<NMMSS::NMediaType::Video::fccY42B>(header, body, 2, 1);
        }

    protected:

        template <typename THeader>
        void DoTransform(typename THeader::SubtypeHeader* header, uint8_t* body, uint8_t uvXFactor, uint8_t uvYFactor)
        {
            Init(header->nWidth, header->nHeight);

            uint32_t dstYPitch = (m_destWidth + 15) & -16;
            uint32_t dstUVPitch = dstYPitch / uvXFactor;
            uint32_t dstUVHeight = m_destHeight / uvYFactor;
            uint32_t dstBodySize = dstYPitch * m_destHeight + 2 * dstUVPitch * dstUVHeight;

            m_dewarpSample = m_allocator->Alloc(dstBodySize);
            m_dewarpSample->Header().nBodySize = dstBodySize;

            uint8_t* dewarpBody = m_dewarpSample->GetBody();
            typename THeader::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<THeader>(m_dewarpSample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(typename THeader::SubtypeHeader));

            subheader->nOffset = 0;
            subheader->nWidth = m_destWidth;
            subheader->nHeight = m_destHeight;
            subheader->nPitch = dstYPitch;
            subheader->nPitchU = dstUVPitch;
            subheader->nPitchV = dstUVPitch;
            subheader->nOffsetU = dstYPitch * m_destHeight;
            subheader->nOffsetV = dstYPitch * m_destHeight + dstUVPitch * dstUVHeight;

            memset(dewarpBody, 0, dstYPitch * m_destHeight);
            memset(dewarpBody + subheader->nOffsetU, 128, dstUVPitch * dstUVHeight);
            memset(dewarpBody + subheader->nOffsetV, 128, dstUVPitch * dstUVHeight);

            MakeDewarp(body,
                       body + header->nOffsetU,
                       body + header->nOffsetV,
                       header->nPitch,
                       header->nPitchU,
                       header->nPitchV,
                       dewarpBody,
                       dewarpBody + subheader->nOffsetU,
                       dewarpBody + subheader->nOffsetV,
                       subheader->nPitch,
                       subheader->nPitchU,
                       subheader->nPitchV,
                       uvXFactor,
                       uvYFactor);

            m_result = NMMSS::ETRANSFORMED;
        }

        void MakeDewarp(uint8_t* pSrcY, uint8_t* pSrcU, uint8_t* pSrcV, uint32_t srcYPitch, uint32_t srcUPitch, uint32_t srcVPitch,
                        uint8_t* pDstY, uint8_t* pDstU, uint8_t* pDstV, uint32_t dstYPitch, uint32_t dstUPitch, uint32_t dstVPitch,
                        uint8_t wUVFactor, uint8_t hUVFactor)
        {
            for (uint32_t y = 0; y < m_destHeight; ++y)
            {
                for (uint32_t x = 0; x < m_destWidth; ++x)
                {
                    uint32_t dstPos = y * dstYPitch + x;

                    uint32_t hook = y * m_destWidth + x;
                    uint32_t srcX = m_hooks[hook].first;
                    uint32_t srcY = m_hooks[hook].second;

                    if (srcX < m_srcWidth && srcY < m_srcHeight)
                    {
                        uint32_t srcPos = srcY * srcYPitch + srcX;
                        pDstY[dstPos] = pSrcY[srcPos];

                        if (pSrcU && pSrcV && pDstU && pDstV)
                        {
                            uint32_t dstPosU = (y / hUVFactor) * dstUPitch + x / wUVFactor;
                            uint32_t dstPosV = (y / hUVFactor) * dstVPitch + x / wUVFactor;

                            uint32_t srcUVx = srcX / wUVFactor;
                            uint32_t srcUVy = srcY / hUVFactor;

                            uint32_t srcPosU = srcUVy * srcUPitch + srcUVx;
                            uint32_t srcPosV = srcUVy * srcVPitch + srcUVx;

                            pDstU[dstPosU] = pSrcU[srcPosU];
                            pDstV[dstPosV] = pSrcV[srcPosV];
                        }
                    }
                }
            }
        }

        virtual glm::vec2 MapViewPortPointToFisheyeCoord(int x, int y) const = 0;

        void Init(uint32_t srcWidth, uint32_t srcHeight)
        {
            if(m_initialized)
                return;

            m_srcWidth = srcWidth;
            m_srcHeight = srcHeight;

            int xRadius = int((m_fisheyeRight - m_fisheyeLeft) / 2.0f * m_srcWidth);
            int yRadius = int((m_fisheyeBottom - m_fisheyeTop) / 2.0f * m_srcHeight);

            int cx = int(m_fisheyeLeft * m_srcWidth + xRadius);
            int cy = int(m_fisheyeTop * m_srcHeight + yRadius);

            m_hooks.resize(m_destWidth * m_destHeight);

            for (int y = 0; y < int(m_destHeight); ++y)
            {
                for (int x = 0; x < int(m_destWidth); ++x)
                {
                    glm::vec2 fisheyePoint = MapViewPortPointToFisheyeCoord(x, y);

                    int srcX = int(fisheyePoint.x * xRadius + cx);
                    int srcY = int(fisheyePoint.y * yRadius + cy);

                    float len = std::sqrt(fisheyePoint.x * fisheyePoint.x + fisheyePoint.y * fisheyePoint.y);
                    if (len > 1.0f)
                    {
                        srcX = -1;
                        srcY = -1;
                    }

                    if (srcX >= 0 && srcX < int(m_srcWidth) && srcY >= 0 && srcY < int(m_srcHeight))
                    {
                        m_hooks[y * m_destWidth + x] = std::make_pair(uint32_t(srcX), uint32_t(srcY));
                    }
                    else
                    {
                        m_hooks[y * m_destWidth + x] = std::make_pair(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max());
                    }
                }
            }

            m_initialized = true;
        }

    protected:

        float m_fisheyeLeft;
        float m_fisheyeRight;
        float m_fisheyeTop;
        float m_fisheyeBottom;
        float m_fov;
        float m_pan;
        float m_tilt;
        float m_zoom;
        NMMSS::ECameraPlace m_place;
        uint32_t m_destWidth;
        uint32_t m_destHeight;
        uint32_t m_srcWidth;
        uint32_t m_srcHeight;

        std::vector<std::pair<uint32_t, uint32_t>> m_hooks;
        NMMSS::PAllocator m_allocator;
        NMMSS::PSample m_dewarpSample;
        NMMSS::ETransformResult m_result;
        bool m_initialized;
    };

    class CFisheyePtzDewarpTransformer : public CFisheyeDewarpTransformer
    {
        glm::mat4 m_projection;
        glm::mat4 m_model;

    public:

        CFisheyePtzDewarpTransformer(DECLARE_LOGGER_ARG, float fisheyeLeft, float fisheyeRight, float fisheyeTop, float fisheyeBottom, float fov, float pan, float tilt, float zoom, uint32_t destWidth, uint32_t destHeight, NMMSS::ECameraPlace place)
            : CFisheyeDewarpTransformer(GET_LOGGER_PTR, fisheyeLeft, fisheyeRight, fisheyeTop, fisheyeBottom, fov, pan, tilt, zoom, destWidth, destHeight, place)
        {
            // check if zoom is extremal
            static const float ZOOM_LIMIT = 1.6f;
            if (m_zoom > ZOOM_LIMIT)
            {
                m_fov -= (m_zoom - ZOOM_LIMIT);
                m_zoom = ZOOM_LIMIT;
            }

            // make projection
            float far = 2.0f;
            float near = 1.0f;
            float aspect = float(m_destWidth) / m_destHeight;

            m_projection = glm::perspective(m_zoom, aspect, near, far);

            // make model
            glm::vec3 at = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 eye = glm::vec3(0.0f, 0.0f, 0.0f);
            glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);

            m_model = glm::lookAt(eye, at, up);

            if (place == NMMSS::ECameraPlace::WALL)
            {
                m_model = glm::rotate(m_model, m_tilt, glm::vec3(1.0f, 0.0f, 0.0f));
                m_model = glm::rotate(m_model, m_pan, glm::vec3(0.0f, 0.0f, 1.0f));
            }
            else if (place == NMMSS::ECameraPlace::CEILING)
            {
                m_model = glm::rotate(m_model, m_tilt, glm::vec3(1.0f, 0.0f, 0.0f));
                m_model = glm::rotate(m_model, m_pan, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            else
            {
                m_model = glm::rotate(m_model, m_tilt, glm::vec3(1.0f, 0.0f, 0.0f));
                m_model = glm::rotate(m_model, m_pan + glm::pi<float>(), glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        glm::vec2 MapViewPortPointToFisheyeCoord(int x, int y) const override
        {
            glm::vec3 vector = glm::normalize(glm::unProject(glm::vec3((float)x, (float)y, 0.0f), m_model, m_projection, glm::vec4(0.0f, 0.0f, (float)m_destWidth, (float)m_destHeight)));

            float r = 2.0f * std::atan2(std::sqrt(vector.x * vector.x + vector.z * vector.z), vector.y) / m_fov;
            float theta = std::atan2(vector.z, vector.x);

            return glm::vec2(r * std::cos(theta), r * std::sin(theta));
        }
    };

    class CFisheyePerimeterDewarpTransformer : public CFisheyeDewarpTransformer
    {
    public:

        CFisheyePerimeterDewarpTransformer(DECLARE_LOGGER_ARG, float fisheyeLeft, float fisheyeRight, float fisheyeTop, float fisheyeBottom, float fov, float pan, float tilt, float zoom, uint32_t destWidth, uint32_t destHeight, NMMSS::ECameraPlace place)
            : CFisheyeDewarpTransformer(GET_LOGGER_PTR, fisheyeLeft, fisheyeRight, fisheyeTop, fisheyeBottom, fov, pan, tilt, zoom, destWidth, destHeight, place)
        {
            static const float ZOOM_LIMIT = 2.356f;  // to cut distortions of poles

            float zoomLimit = m_place == NMMSS::ECameraPlace::WALL ? ZOOM_LIMIT : ZOOM_LIMIT / 2.0f;

            if (m_zoom > zoomLimit)
                m_zoom = zoomLimit;
        }

        glm::vec2 MapViewPortPointToFisheyeCoord(int x, int y) const override
        {
            if (m_place == NMMSS::ECameraPlace::WALL)
            {
                float xScale = m_zoom * float(m_destWidth) / float(m_destHeight);

                float lon = (float(x) / float(m_destWidth - 1) - 0.5f) * xScale - glm::pi<float>() / 2.0f + m_pan;
                float lat = (float(y) / float(m_destHeight - 1) - 0.5f) * m_zoom - m_tilt;

                glm::vec3 vector(std::cos(lat) * std::cos(-lon), std::cos(lat) * std::sin(-lon), std::sin(lat));

                float r = 2.0f * std::atan2(std::sqrt(vector.x * vector.x + vector.z * vector.z), vector.y) / glm::pi<float>();
                float theta = std::atan2(vector.z, vector.x);

                return glm::vec2(r * std::cos(theta), r * std::sin(theta));
            }

            float xScale = m_zoom * float(m_destWidth) / float(m_destHeight);
            float yScale = m_zoom / m_fov * 2.0f;

            float theta = (float(x) / float(m_destWidth - 1) - 0.5f) * xScale - glm::pi<float>() / 2.0f + m_pan;
            float r = m_tilt * 2.0f / glm::pi<float>() - yScale * (float(y) / float(m_destHeight - 1) - 0.5f);

            return glm::vec2(r * std::cos(theta), (m_place == NMMSS::ECameraPlace::CEILING ? 1.0f : -1.0f) * r * std::sin(theta));
        }
    };
}

namespace NMMSS
{
    IFilter* CreateFisheyeDewarpFilter(DECLARE_LOGGER_ARG, float fisheyeLeft, float fisheyeRight, float fisheyeTop, float fisheyeBottom, float fov, float pan, float tilt, float zoom, uint32_t destWidth, uint32_t destHeight, ECameraPlace place, EDewarpMode mode)
    {
        _log_ << "fisheye: l=" << fisheyeLeft << " r=" << fisheyeRight << " t=" << fisheyeTop << " b=" << fisheyeBottom << " fov=" << fov << " pan=" << pan << " tilt=" << tilt << " zoom=" << zoom << " place=" << place << " width=" << destWidth << " height=" << destHeight << " mode=" << mode;

        if (mode == NMMSS::EDewarpMode::PERIMETER)
        {
            return new CPullFilterImpl<CFisheyePerimeterDewarpTransformer, true>(
                GET_LOGGER_PTR,
                SAllocatorRequirements(0),
                SAllocatorRequirements(0),
                new CFisheyePerimeterDewarpTransformer(GET_LOGGER_PTR, fisheyeLeft, fisheyeRight, fisheyeTop, fisheyeBottom, fov, pan, tilt, zoom, destWidth, destHeight, place)
                );
        }

        return new CPullFilterImpl<CFisheyePtzDewarpTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CFisheyePtzDewarpTransformer(GET_LOGGER_PTR, fisheyeLeft, fisheyeRight, fisheyeTop, fisheyeBottom, fov, pan, tilt, zoom, destWidth, destHeight, place)
            );
    }
}
