#ifndef NGP_FFMPEG_ALLOCATOR__H
#define NGP_FFMPEG_ALLOCATOR__H

#include "../ConnectionBroker.h"
#include <boost/interprocess/detail/atomic.hpp>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

#if defined(_WIN32)
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // warning C4244: '=' : conversion from '__int64' to 'int', possible loss of data
#endif // _MSC_VER

namespace NMMSS
{
    /**!
       \brief Класс CFFmpegAllocator используется для реализации
       декодирования FFmpeg-ом в заданную область памяти, выделенную
       аллокатором внутри MMSS.
       Указатель на данный объект должен передаваться в качестве параметра
       "opaque" контексту AVCodecContext.

       Параметр "opaque" у AVFrame содержит указатель на соответствующий сэмпл.
    */
    class CFFmpegAllocator
    {
    public:
        CFFmpegAllocator(IAllocator* allocator)
            : m_allocator(allocator, NCorbaHelpers::ShareOwnership())
        {
        }

        static PSample ExtractSampleFromFrame(AVFrame* f)
        {
            return NCorbaHelpers::ShareRefcounted(static_cast<ISample*>(f->opaque));
        }

        /* Методы, передаваемые в качестве параметров в AVCodecContext */
        static int get_buffer2(struct AVCodecContext *c, AVFrame *pic, int flags)
        {
            switch (c->codec_type)
            {
            case AVMEDIA_TYPE_VIDEO:
            {
                pic->width = c->coded_width;
                pic->height = c->coded_height;
                pic->format = c->pix_fmt;

                CFFmpegAllocator* pObj = (CFFmpegAllocator*)(c->opaque);
                return pObj ? pObj->get_buffer2_impl(c, pic, flags) : -1;
            }
            case AVMEDIA_TYPE_AUDIO:
                return avcodec_default_get_buffer2(c, pic, flags);
            default:
                return -1;
            }
        }

        static void release_buffer2(void* opaque, uint8_t* data)
        {
            // pSample into opaque.
            if (data)
            {
                data[0] = 0;
            }

            if (0 != opaque)
            {
                ISample* pSample = (ISample*)(opaque);
                pSample->Release();
            }
        }

        int get_buffer2_impl(AVCodecContext* avctx, AVFrame *pic, int flags)
        {
            int h_chroma_shift, v_chroma_shift;
            int i, unaligned;
            size_t size[4] = { 0 };
            int offset[4] = { 0 };
            uint8_t* data[4];

            if (!m_allocator)
            {
                return -1;
            }

            int w = pic->width,
                h = pic->height;

            const AVPixelFormat pix_fmt = static_cast<AVPixelFormat>(pic->format);

            if (av_image_check_size(w, h, 0, NULL) < 0)
            {
                return AVERROR(EINVAL);
            }

            memset(pic->data, 0, sizeof(pic->data));

            avcodec_get_chroma_sub_sample(pix_fmt, &h_chroma_shift, &v_chroma_shift);

            // Maximum alignment required is for AVX; use it to be on the safe side
            const int stride_align[4] = { 32, 32, 32, 32 };
            {
                // inlined stripped-down version of avcodec_align_dimensions2
                const int PIXEL_PER_MACROBLOCK = 16;            // assume 16 pixel per macroblock
                const int w_align = PIXEL_PER_MACROBLOCK;
                const int h_align = PIXEL_PER_MACROBLOCK * 2;   // interlaced needs 2 macroblocks height
                w = FFALIGN(w, w_align);
                h = FFALIGN(h, h_align);
            }

            do{
                av_image_fill_linesizes(pic->linesize, pix_fmt, w);
                w += w & ~(w - 1);

                unaligned = 0;
                for (i = 0; i < 4; i++){
                    unaligned |= pic->linesize[i] % stride_align[i];
                }
            } while (unaligned);


            int image_size = av_image_fill_pointers(data, pix_fmt,
                h, NULL, pic->linesize);

            if (image_size < 0)
            {
                return AVERROR(EINVAL);
            }

            for (i = 0; i < 3 && data[i + 1]; i++)
                size[i] = data[i + 1] - data[i];
            size[i] = image_size - (data[i] - data[0]);

            image_size = 0;
            for (i = 0; i < 4 && size[i]; ++i)
            {
                offset[i] = 0;

                size[i] += AV_INPUT_BUFFER_PADDING_SIZE; //32 bytes at the end of each plane
                image_size += size[i] + offset[i];
            }

            ISample* pSample = m_allocator->Alloc(image_size);
            if (!pSample)
            {
                return AVERROR(ENOMEM);
            }

            NMMSS::SMediaSampleHeader& header = pSample->Header();
            header.nBodySize = image_size;

            uint8_t *body = pSample->GetBody();

            pic->data[0] = body + offset[0];

            pic->buf[0] = av_buffer_create(pic->data[0], image_size, release_buffer2, pSample, 0);
            if (!pic->buf[0])
            {
                pSample->Release();
                return AVERROR(ENOMEM);
            }

            for (i = 1; i < 4 && size[i]; ++i)
            {
                pic->data[i] = pic->data[i - 1] + size[i - 1] + offset[i];
            }

            pic->opaque = pSample;

            return 0;
        }

    protected:
        NMMSS::PAllocator m_allocator;
    };
}

#ifdef _MSC_VER
#pragma warning(pop) 
#endif // _MSC_VER

#endif //NGP_FFMPEG_ALLOCATOR__H
