#include "FrameBuilder.h"
#include "../MediaType.h"
#include "../Sample.h"
#include <CorbaHelpers/RefcountedImpl.h>
#include "../Codecs/MPEG4/include/mpeg4cookies.h"
#ifdef _WIN32
#include <WaveCommon3.h>
#endif

#include "FrameInfo.h"

namespace
{
    class CFramePreprocessor : public NMMSS::IFrameBuilder, public NCorbaHelpers::CRefcountedImpl
    {
    public:
        NMMSS::PSample PreprocessSample(NMMSS::PSample pSample, NMMSS::IAllocator*, size_t) override
        {
            return pSample;
        }

        void Restart() override
        {
            m_markNextSampleWithDiscontinuity = true;
            m_frameInfo = {};
        }

    protected:
        void SetFrameInfo(const NMMSS::CFrameInfo& info)
        {
            if (info.width != m_frameInfo.width || info.height != m_frameInfo.height || 
                info.profile != m_frameInfo.profile || info.level != m_frameInfo.level)
            {
                m_frameInfo = info;
                m_markNextSampleWithDiscontinuity = true;
            }
        }

        using FrameInfoGetter = bool(NMMSS::CFrameInfo&, const uint8_t*, int);

        void SetFrameInfo(NMMSS::ISample* sample, FrameInfoGetter* getter)
        {
            NMMSS::CFrameInfo info;
            if (getter(info, sample->GetBody(), sample->Header().nBodySize))
            {
                SetFrameInfo(info);
            }
        }

        template <typename TMediaType>
        bool SetupHeader(NMMSS::ISample* sample, bool allowEmptyResolution = false)
        {
            auto& header = sample->SubHeader<TMediaType>();
            header.nCodedWidth = m_frameInfo.width;
            header.nCodedHeight = m_frameInfo.height;
            if (m_frameInfo.width && m_frameInfo.height)
            {
                if (m_markNextSampleWithDiscontinuity)
                {
                    sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFDiscontinuity;
                    m_markNextSampleWithDiscontinuity = false;
                }
                return true;
            }
            else if (allowEmptyResolution)
            {
                // Skip all samples to the next keyframe.
                sample->Header().eFlags = NMMSS::SMediaSampleHeader::EFNeedPreviousFrame | NMMSS::SMediaSampleHeader::EFAfterSkipped;
            }
            return allowEmptyResolution;
        }

    private:
        bool m_markNextSampleWithDiscontinuity = true;
        NMMSS::CFrameInfo m_frameInfo;
    };

    class CMPEG2FrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample) override
        {
            SetFrameInfo(pSample, NMMSS::FindFrameInfoMPEG2);
            return SetupHeader<NMMSS::NMediaType::Video::fccMPEG2>(pSample, true);
        }
    };

    class CMPEG4FrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample) override
        {
            auto& header = pSample->Header();
            int res = mpeg4_cookies_info(&pInfo, &pTime, pSample->GetBody(), header.nBodySize);
            if (res >= 0)
            {
                NMMSS::NMediaType::Video::fccMPEG4::SubtypeHeader *subheader = 0;
                NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccMPEG4>(pSample->GetHeader(),
                    &subheader);

                if (0 == res)
                {
                    SetFrameInfo(NMMSS::CFrameInfo{ (int)pInfo.width, (int)pInfo.height });
                }
                else
                {
                    header.eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame |
                        NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
                }
                return SetupHeader<NMMSS::NMediaType::Video::fccMPEG4>(pSample);
            }
            return false;
        }

    private:
        MPEG4_FRAME_INFO pInfo = {};
        MPEG4_TIME_INFO pTime = {};
    };

    class CJPEGFrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample) override
        {
            NMMSS::NMediaType::Video::fccJPEG::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccJPEG>(
                pSample->GetHeader(), &subheader);

            NMMSS::CFrameInfo info;
            if (NMMSS::FindFrameInfoJPEG(info, pSample->GetBody(), pSample->Header().nBodySize))
            {
                pSample->Header().eFlags = 0;
                SetFrameInfo(info);
                return SetupHeader<NMMSS::NMediaType::Video::fccJPEG>(pSample);
            }
            return false;
        }
    };

    class CJPEG2000FrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample) override
        {
            NMMSS::NMediaType::Video::fccJPEG2000::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccJPEG2000>(
                pSample->GetHeader(), &subheader);

            NMMSS::CFrameInfo info;
            if (NMMSS::FindFrameInfoJPEG2000(info, subheader->nResolutionLevels, pSample->GetBody(), pSample->Header().nBodySize))
            {
                pSample->Header().eFlags = 0;
                SetFrameInfo(info);
                return SetupHeader<NMMSS::NMediaType::Video::fccJPEG2000>(pSample);
            }
            return false;
        }
    };

    class CMXPEGFrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample) override
        {
            NMMSS::NMediaType::Video::fccMXPEG::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccMXPEG>(
                pSample->GetHeader(), &subheader);

            NMMSS::CFrameInfo info;
            if (NMMSS::FindFrameInfoJPEG(info, pSample->GetBody(), pSample->Header().nBodySize))
            {
                pSample->Header().eFlags = 0;
                SetFrameInfo(info);
            }
            else
            {
                pSample->Header().eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame |
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
            return SetupHeader<NMMSS::NMediaType::Video::fccMXPEG>(pSample);
        }
    };

    class CH264FrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample) override
        {
            if(pSample->Header().IsKeySample())
            {
                SetFrameInfo(pSample, NMMSS::FindFrameInfoH264);
            }
            return SetupHeader<NMMSS::NMediaType::Video::fccH264>(pSample, true);
        }
    };

    class CH265FrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample) override
        {
            if (pSample->Header().IsKeySample())
            {
                SetFrameInfo(pSample, NMMSS::FindFrameInfoH265);
            }
            return SetupHeader<NMMSS::NMediaType::Video::fccH265>(pSample);
        }
    };

    class CVP8FrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample) override
        {
            if (pSample->Header().IsKeySample())
            {
                SetFrameInfo(pSample, NMMSS::FindFrameInfoVP8);
            }
            return SetupHeader<NMMSS::NMediaType::Video::fccVP8>(pSample);
        }
    };

    class CVP9FrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample) override
        {
            if (pSample->Header().IsKeySample())
            {
                SetFrameInfo(pSample, NMMSS::FindFrameInfoVP9);
            }
            return SetupHeader<NMMSS::NMediaType::Video::fccVP9>(pSample);
        }
    };

    class CMP2FrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample)
        {
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::MP2>(pSample->GetHeader());
            return true;
        }
    };

    class CAACFrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample* pSample)
        {
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::AAC>(pSample->GetHeader());
            return true;
        }
    };

    class CGreyFrameBuilder : public CFramePreprocessor
    {
    public:
        CGreyFrameBuilder(uint32_t width, uint32_t height)
            : m_width(width)
            , m_height(height)
        {
        }

        bool InitializeSubheader(NMMSS::ISample* pSample)
        {
            NMMSS::NMediaType::Video::fccGREY::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(pSample->GetHeader(),
                &subheader);
            subheader->nWidth = subheader->nPitch = m_width;
            subheader->nHeight = m_height;
            subheader->nOffset = 0;
            return true;
        }

    private:
        uint32_t m_width;
        uint32_t m_height;
    };

    class CI420FrameBuilder : public CFramePreprocessor
    {
    public:
        CI420FrameBuilder(uint32_t width, uint32_t height)
            : m_width(width)
            , m_height(height)
        {
        }

        bool InitializeSubheader(NMMSS::ISample* pSample)
        {
            NMMSS::NMediaType::Video::fccI420::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccI420>(pSample->GetHeader(),
                &subheader);
            subheader->nWidth = subheader->nPitch = m_width;
            subheader->nHeight = m_height;
            subheader->nOffset = 0;
            subheader->nPitchU = subheader->nPitchV = m_width / 2;
            subheader->nOffsetU = m_height * m_width;
            subheader->nOffsetV = subheader->nOffsetU + (m_height * m_width)/4;
            return true;
        }

    private:
        uint32_t m_width;
        uint32_t m_height;
    };

    class CY42BFrameBuilder : public CFramePreprocessor
    {
    public:
        CY42BFrameBuilder(uint32_t width, uint32_t height)
            : m_width(width)
            , m_height(height)
        {
        }

        bool InitializeSubheader(NMMSS::ISample* pSample)
        {
            NMMSS::NMediaType::Video::fccI420::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccI420>(pSample->GetHeader(),
                &subheader);
            subheader->nWidth  = subheader->nPitch = m_width;
            subheader->nHeight = m_height;
            subheader->nOffset = 0;
            subheader->nPitchU = subheader->nPitchV = m_width / 2;
            subheader->nOffsetU = m_height * m_width;
            subheader->nOffsetV = subheader->nOffsetU + (m_height * m_width) / 2;
            return true;
        }

    private:
        uint32_t m_width;
        uint32_t m_height;
    };

#ifdef _WIN32

    class CIntellectFrameBuilder : public CFramePreprocessor
    {
    public:
        CIntellectFrameBuilder(uint32_t width, uint32_t height)
            : m_width(width)
            , m_height(height)
        {
        }

        bool InitializeSubheader(NMMSS::ISample* pSample)
        {
            NMMSS::NMediaType::Video::fccITV::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccITV>(pSample->GetHeader(),
                &subheader);
            subheader->nCodedWidth = m_width;
            subheader->nCodedHeight = m_height;
            TFrameHead* frameHeader = reinterpret_cast<TFrameHead*>(pSample->GetBody());
            if(frameHeader->Flags & 0x0001)
            {
                NMMSS::SMediaSampleHeader& header = pSample->Header();
                header.eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame;
            }

            return true;
        }

    private:
        uint32_t m_width;
        uint32_t m_height;
    };

#endif

    class CNullFrameBuilder : public CFramePreprocessor
    {
    public:
        bool InitializeSubheader(NMMSS::ISample*)
        {
            return true;
        }
    };
}

namespace NMMSS
{
    IFrameBuilder* CreateMPEG2FrameBuilder()
    {
        return new CMPEG2FrameBuilder;
    }

    IFrameBuilder* CreateMPEG4FrameBuilder()
    {
        return new CMPEG4FrameBuilder;
    }

    IFrameBuilder* CreateJPEGFrameBuilder()
    {
        return new CJPEGFrameBuilder;
    }

    IFrameBuilder* CreateJPEG2000FrameBuilder()
    {
        return new CJPEG2000FrameBuilder;
    }

    IFrameBuilder* CreateMXPEGFrameBuilder()
    {
        return new CMXPEGFrameBuilder;
    }

    IFrameBuilder* CreateH264FrameBuilder()
    {
        return new CH264FrameBuilder();
    }

    IFrameBuilder* CreateH265FrameBuilder()
    {
        return new CH265FrameBuilder();
    }

    IFrameBuilder* CreateVP8FrameBuilder()
    {
        return new CVP8FrameBuilder();
    }

    IFrameBuilder* CreateVP9FrameBuilder()
    {
        return new CVP9FrameBuilder();
    }

    IFrameBuilder* CreateMP2FrameBuilder()
    {
        return new CMP2FrameBuilder;
    }

    IFrameBuilder* CreateAACFrameBuilder()
    {
        return new CAACFrameBuilder;
    }

    IFrameBuilder* CreateGreyFrameBuilder(uint32_t width, uint32_t height)
    {
        return new CGreyFrameBuilder(width, height);
    }

    IFrameBuilder* CreateI420FrameBuilder(uint32_t width, uint32_t height)
    {
        return new CI420FrameBuilder(width, height);
    }

    IFrameBuilder* CreateY42BFrameBuilder(uint32_t width, uint32_t height)
    {
        return new CY42BFrameBuilder(width, height);
    }

#ifdef _WIN32

    IFrameBuilder* CreateIntellectFrameBuilder(uint32_t width, uint32_t height)
    {
        return new CIntellectFrameBuilder(width, height);
    }

    IFrameBuilder* CreateWavecamFrameBuilder()
    {
        return new CNullFrameBuilder;
    }

#endif

    IFrameBuilder* CreateNullFrameBuilder()
    {
        return new CNullFrameBuilder;
    }
}
