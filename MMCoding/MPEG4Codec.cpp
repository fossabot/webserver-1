#include <boost/lexical_cast.hpp>
#include "../MiscFilters.h"
#include "../FilterImpl.h"
#include "../MediaType.h"
#include "FFMPEGCodec.h"
#include "../PtimeFromQword.h"

static const int nMaxCompressedFrameSize=2*1600*1200;

using namespace NMMSS;

#ifdef _MSC_VER
#pragma warning(push)
#endif // _MSC_VER

namespace
{
    inline NMMSS::AVFramePtr AllocAVFrame() { return NMMSS::AVFramePtr{ av_frame_alloc() }; }

    class CFFMPEGEncoderHelper
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CFFMPEGEncoderHelper(DECLARE_LOGGER_ARG,
                             NMMSS::CDeferredAllocSampleHolder& holder,
                             CFFMPEGVideoEncoder* codec,
                             uint32_t bodySize,
                             boost::posix_time::ptime startTime,
                             int64_t& pts)
            :   m_holder(holder)
            ,   m_codec(codec)
            ,   m_bodySize(bodySize)
            ,   m_result(NMMSS::ETHROUGH)
            ,   m_startTime(startTime)
            ,   m_pts(pts)
        {
            INIT_LOGGER_HOLDER;
        }

        ~CFFMPEGEncoderHelper()
        {
        }


        NMMSS::ETransformResult GetResult() const
        {
            return m_result;
        }

        bool IsKeyFrame() const
        {
            return false;
        }
    protected:
        virtual void CreateHeader(uint32_t bodySize, bool isKeyFrame, int width, int height) = 0;
        virtual AVCodecContext* GetCodexContext(int codedWidth, int codedHeight) = 0;

        NMMSS::AVFramePtr Convert(uint8_t* body, NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header)
        {
            auto pDestFrame = AllocAVFrame();
            if (0 == pDestFrame)
            {
                _log_ << "Couldn't alloc frame for conversion" << std::endl;
                return 0;
            }

            int width  = header->nWidth;
            int height = header->nHeight;
            int pitch  = header->nPitch;

            int outChromaSize = width*height/2;

            m_pSampleUV = m_holder.AllocUnmanaged(outChromaSize);
            if (!m_pSampleUV)
            {
                _log_ << "GRAY YUV memory allocation failed" << std::endl;
                return 0;
            }

            memset(m_pSampleUV->GetBody(), 128, outChromaSize);

            pDestFrame->linesize[0] = pitch;
            pDestFrame->linesize[1] = width/2;
            pDestFrame->linesize[2] = width/2;
            pDestFrame->data[0] = body + header->nOffset;
            pDestFrame->data[1] = m_pSampleUV->GetBody();
            pDestFrame->data[2] = pDestFrame->data[1] + outChromaSize/2;

            return pDestFrame;
        }

        NMMSS::AVFramePtr Convert(uint8_t* body, NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header)
        {
            auto pDestFrame = AllocAVFrame();
            if (0 == pDestFrame)
            {
                _log_ << "Couldn't alloc frame for conversion" << std::endl;
                return 0;
            }

            int width  = header->nWidth;
            int height = header->nHeight;
            int pitch  = header->nPitch;

            int outChromaSize = width*height/2;

            m_pSampleUV = m_holder.AllocUnmanaged(outChromaSize);
            if (!m_pSampleUV)
            {
                _log_ << "YUV422 YUV memory allocation failed" << std::endl;
                return 0;
            }

            uint8_t* pChromaBuffer = m_pSampleUV->GetBody();

            for(int i = 0; i < height/2; ++i)
            {
                for(int j = 0; j < width/2; ++j)
                {
                    pChromaBuffer[(width/2)*i + j] =
                        body[header->nOffsetU + width*i + j];

                    pChromaBuffer[(width/2)*i + j + outChromaSize/2] =
                        body[header->nOffsetV + width*i + j];
                }
            }


            pDestFrame->linesize[0] = pitch;
            pDestFrame->linesize[1] = width/2;
            pDestFrame->linesize[2] = width/2;
            pDestFrame->data[0] = body + header->nOffset;
            pDestFrame->data[1] = pChromaBuffer;
            pDestFrame->data[2] = pChromaBuffer + outChromaSize/2;

            return pDestFrame;
        }

        void EncodeAndWrite(AVCodecContext* pContext, AVFrame* pFrame, int width, int height)
        {
            NMMSS::SMediaSampleHeader& h = m_holder->Header();
            boost::posix_time::ptime sampleTime = NMMSS::PtimeFromQword(h.dtTimeBegin);

            boost::posix_time::time_duration td = sampleTime - m_startTime;
            int64_t pts = ((int64_t)(td.total_milliseconds())*50 +  500)/1000;

            while(pts <= m_pts) ++pts;

            pFrame->pts = m_pts = pts;

            AVPacket pkt = { 0 };
            av_init_packet(&pkt);
            pkt.data = m_holder->GetBody();
            pkt.size = std::max(AV_INPUT_BUFFER_MIN_SIZE, 3 * width * height);

            int got_packet = 0;

            int ret = avcodec_encode_video2(pContext, &pkt, pFrame, &got_packet);
            if (!ret && got_packet && pContext->coded_frame)
            {
                pContext->coded_frame->pts = pkt.pts;
                pContext->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);

                if (pkt.size > nMaxCompressedFrameSize)
                {
                    _err_ << "ERROR: Too big frame size = " << pkt.size << std::endl;
                }

                CreateHeader(pkt.size, (pContext->coded_frame->key_frame != 0),
                    pContext->coded_width, pContext->coded_height);
                m_result = NMMSS::ETRANSFORMED;
            }
            else
            {
                if (ret)
                {
                    std::vector<char> message(1000);
                    av_strerror(ret, &message[0], message.size());

                    _err_ << "Error while encode video with FFmpeg: (" << boost::lexical_cast<std::string>(ret) + ") " << message.data();
                }
                m_result = NMMSS::EIGNORED;
            }

            av_packet_free_side_data(&pkt);
        }

        NMMSS::CDeferredAllocSampleHolder& m_holder;
        CFFMPEGVideoEncoder* m_codec;
        uint32_t m_bodySize;
        NMMSS::ETransformResult m_result;
        boost::posix_time::ptime m_startTime;
        NMMSS::PSample m_pSampleUV;
        int64_t& m_pts;
    };

    class CMPEG4EncoderHelper : public CFFMPEGEncoderHelper
    {
    public:
        CMPEG4EncoderHelper(DECLARE_LOGGER_ARG,
                            NMMSS::CDeferredAllocSampleHolder& holder,
                            CFFMPEGVideoEncoder* codec,
                            uint32_t bodySize,
                            boost::posix_time::ptime startTime,
                            int64_t& pts) :
            CFFMPEGEncoderHelper(GET_LOGGER_PTR,
                                 holder,
                                 codec,
                                 bodySize,
                                 startTime,
                                 pts)
        {
        }

        virtual void CreateHeader(uint32_t bodySize, bool isKeyFrame, int width, int height)
        {
            NMMSS::NMediaType::Video::fccMPEG4::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccMPEG4>(
                m_holder->GetHeader(), &subheader);
            subheader->nCodedWidth = width;
            subheader->nCodedHeight = height;

            NMMSS::SMediaSampleHeader* header = reinterpret_cast<NMMSS::SMediaSampleHeader*>(m_holder->GetHeader());
            header->nBodySize = bodySize;
            if (!isKeyFrame)
                header->eFlags |= NMMSS::SMediaSampleHeader::EFNeedKeyFrame|NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
        }

        virtual AVCodecContext* GetCodexContext(int codedWidth, int codedHeight)
        {
            return m_codec->GetMPEG4Context(codedWidth, codedHeight);
        }

        template<typename TMediaTypeHeader>
        void operator()(TMediaTypeHeader* pHeader, uint8_t* pData)
        {
        }

        void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header, uint8_t* body)
        {
            AVCodecContext* pContext = GetCodexContext(header->nWidth, header->nHeight);
            if (0 == pContext)
                return;

            NMMSS::AVFramePtr autoFrame( Convert(body, header) );
            if (0 == autoFrame.get())
                return;


            EncodeAndWrite(pContext, autoFrame.get(), header->nWidth, header->nHeight);
        }

        void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* body)
        {
            AVCodecContext* pContext = GetCodexContext(header->nWidth, header->nHeight);
            if (0 == pContext)
                return;

            auto pFrame = AllocAVFrame();
            if (0 == pFrame)
                return;

            avpicture_fill(reinterpret_cast<AVPicture *>(pFrame.get()), body, AV_PIX_FMT_YUV420P,
                header->nPitch, header->nHeight);
            pFrame->data[0] = (uint8_t*)(body) + header->nOffset;
            pFrame->data[1] = (uint8_t*)(body) + header->nOffsetU;
            pFrame->data[2] = (uint8_t*)(body) + header->nOffsetV;

            EncodeAndWrite(pContext, pFrame.get(), header->nWidth, header->nHeight);
        }

        void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* body)
        {
            AVCodecContext* pContext = GetCodexContext(header->nWidth, header->nHeight);
            if (0 == pContext)
                return;

            NMMSS::AVFramePtr autoFrame(Convert(body, header));
            if (0 == autoFrame.get())
                return;

            EncodeAndWrite(pContext, autoFrame.get(), header->nWidth, header->nHeight);
        }
    };


    class CH264EncoderHelper : public CMPEG4EncoderHelper
    {
    public:
        CH264EncoderHelper(DECLARE_LOGGER_ARG,
            NMMSS::CDeferredAllocSampleHolder& holder,
            CFFMPEGVideoEncoder* codec,
            uint32_t bodySize,
            boost::posix_time::ptime startTime,
            int64_t& pts) :
            CMPEG4EncoderHelper(GET_LOGGER_PTR,
                holder,
                codec,
                bodySize,
                startTime,
                pts)
        {
        }

        virtual void CreateHeader(uint32_t bodySize, bool isKeyFrame, int width, int height)
        {
            NMMSS::NMediaType::Video::fccH264::SubtypeHeader* subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccH264>(
                m_holder->GetHeader(), &subheader);
            subheader->nCodedWidth = width;
            subheader->nCodedHeight = height;

            NMMSS::SMediaSampleHeader* header = reinterpret_cast<NMMSS::SMediaSampleHeader*>(m_holder->GetHeader());
            header->nBodySize = bodySize;
            if (!isKeyFrame)
                header->eFlags |= NMMSS::SMediaSampleHeader::EFNeedKeyFrame | NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
        }

        virtual AVCodecContext* GetCodexContext(int codedWidth, int codedHeight)
        {
            return m_codec->GetH264Context(codedWidth, codedHeight);
        }
    };


    class CMJPEGEncoderHelper : public CFFMPEGEncoderHelper
    {
    public:
        CMJPEGEncoderHelper(DECLARE_LOGGER_ARG,
                            NMMSS::CDeferredAllocSampleHolder& holder,
                            CFFMPEGVideoEncoder* codec,
                            uint32_t bodySize,
                            boost::posix_time::ptime startTime,
                            int64_t& pts) :
            CFFMPEGEncoderHelper(GET_LOGGER_PTR,
                                 holder,
                                 codec,
                                 bodySize,
                                 startTime,
                                 pts)
        {
        }

        virtual void CreateHeader(uint32_t bodySize, bool isKeyFrame, int width, int height)
        {
            NMMSS::NMediaType::Video::fccJPEG::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccJPEG>(
                m_holder->GetHeader(), &subheader);

            NMMSS::SMediaSampleHeader* header = reinterpret_cast<NMMSS::SMediaSampleHeader*>(m_holder->GetHeader());
            header->nBodySize = bodySize;
        }

        virtual AVCodecContext* GetCodexContext(int codedWidth, int codedHeight)
        {
            return m_codec->GetJPEGContext(AV_PIX_FMT_YUVJ420P, codedWidth, codedHeight);
        }

        template<typename TMediaTypeHeader>
        void operator()(TMediaTypeHeader* pHeader, uint8_t* pData)
        {
        }

        void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header, uint8_t* body)
        {
            AVCodecContext* pContext = GetCodexContext(header->nWidth, header->nHeight);
            if (0 == pContext)
                return;

            NMMSS::AVFramePtr autoFrame( Convert(body, header) );
            if (0 == autoFrame.get())
                return;

            EncodeAndWrite(pContext, autoFrame.get(), header->nWidth, header->nHeight);
        }

        void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* body)
        {
            AVCodecContext* pContext = GetCodexContext(header->nWidth, header->nHeight);
            if (0 == pContext)
                return;

            auto pFrame = AllocAVFrame();
            if (0 == pFrame)
                return;

            avpicture_fill(reinterpret_cast<AVPicture *>(pFrame.get()),
                           (uint8_t*)body,
                           AV_PIX_FMT_YUVJ420P,
                           header->nPitch, header->nHeight);
            pFrame->data[0] = (uint8_t*)(body) + header->nOffset;
            pFrame->data[1] = (uint8_t*)(body) + header->nOffsetU;
            pFrame->data[2] = (uint8_t*)(body) + header->nOffsetV;
            pFrame->linesize[0] = header->nPitch;
            pFrame->linesize[1] = header->nPitchU;
            pFrame->linesize[2] = header->nPitchV;


            EncodeAndWrite(pContext, pFrame.get(), header->nWidth, header->nHeight);
        }

        void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* body)
        {
            AVCodecContext* pContext = GetCodexContext(header->nWidth, header->nHeight);
            if (0 == pContext)
                return;

            auto pFrame = AllocAVFrame();
            if (0 == pFrame)
                return;

            avpicture_fill(reinterpret_cast<AVPicture *>(pFrame.get()),
                           (uint8_t*)body,
                           AV_PIX_FMT_YUVJ422P,
                           header->nPitch, header->nHeight);
            pFrame->data[0] = (uint8_t*)(body) + header->nOffset;
            pFrame->data[1] = (uint8_t*)(body) + header->nOffsetU;
            pFrame->data[2] = (uint8_t*)(body) + header->nOffsetV;
            pFrame->linesize[0] = header->nPitch;
            pFrame->linesize[1] = header->nPitchU;
            pFrame->linesize[2] = header->nPitchV;

            EncodeAndWrite(pContext, pFrame.get(), header->nWidth, header->nHeight);
        }
    };


    template<class TEncoderHelper>
    class CEncoder
    {
        DECLARE_LOGGER_HOLDER;
    public:
        NMMSS::ETransformResult operator()(NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            //для кодера выделяем максимально возможный размер
            if(!holder.Alloc(nMaxCompressedFrameSize))
            {
                _log_ << "Encoder memory allocation failed" << std::endl;
                return NMMSS::EFAILED;
            }

            NMMSS::SMediaSampleHeader& h = holder->Header();
            h.dtTimeBegin = in->Header().dtTimeBegin;
            h.dtTimeEnd = in->Header().dtTimeEnd;

            if (m_startTime.is_not_a_date_time())
            {
                m_startTime = NMMSS::PtimeFromQword(h.dtTimeBegin);
                m_pts = -1;
            }

            TEncoderHelper eh(GET_LOGGER_PTR, holder, &m_codec, in->Header().nBodySize, m_startTime, m_pts);
            NMMSS::NMediaType::ApplyMediaTypeVisitor(in, eh);

            return eh.GetResult();
        }
        CEncoder(DECLARE_LOGGER_ARG, NMMSS::EVideoCodingPreset quality = NMMSS::VCP_BestQuality)
            : m_codec(GET_LOGGER_PTR, quality)
            , m_startTime(boost::posix_time::not_a_date_time)
        {
            INIT_LOGGER_HOLDER;
        }
    private:
        CFFMPEGVideoEncoder m_codec;
        boost::posix_time::ptime m_startTime;
        int64_t m_pts;
    };
}

namespace NMMSS
{
    IFilter* CreateMPEG4EncoderFilter(DECLARE_LOGGER_ARG, NMMSS::EVideoCodingPreset quality)
    {
        return new CPullFilterImpl<CEncoder<CMPEG4EncoderHelper>, true>(GET_LOGGER_PTR,
            SAllocatorRequirements(8, nMaxCompressedFrameSize, 0),
            SAllocatorRequirements(0, 0, 16),
            new CEncoder<CMPEG4EncoderHelper>(GET_LOGGER_PTR, quality));
    }

    IFilter* CreateH264EncoderFilter(DECLARE_LOGGER_ARG, NMMSS::EVideoCodingPreset quality)
    {
        return new CPullFilterImpl<CEncoder<CH264EncoderHelper>, true>(GET_LOGGER_PTR,
            SAllocatorRequirements(8, nMaxCompressedFrameSize, 0),
            SAllocatorRequirements(0, 0, 16),
            new CEncoder<CH264EncoderHelper>(GET_LOGGER_PTR, quality));
    }

    IFilter* CreateMJPEGEncoderFilter(DECLARE_LOGGER_ARG, NMMSS::EVideoCodingPreset quality)
    {
        return new CPullFilterImpl<CEncoder<CMJPEGEncoderHelper>, true>(GET_LOGGER_PTR,
            SAllocatorRequirements(8, nMaxCompressedFrameSize, 0),
            SAllocatorRequirements(0, 0, 16),
            new CEncoder<CMJPEGEncoderHelper>(GET_LOGGER_PTR, quality));
    }
}

#ifdef _MSC_VER
#pragma warning(pop) 
#endif // _MSC_VER
