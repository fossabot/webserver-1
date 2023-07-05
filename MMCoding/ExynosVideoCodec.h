#ifndef _MMCODING_VIDEO_CODEC_EXYNOS_H
#define _MMCODING_VIDEO_CODEC_EXYNOS_H

#include <V4L2Device.h>
#include <Logging/log2.h>

namespace NExynos
{

class VideoDecoder
{
public:
  VideoDecoder();
  ~VideoDecoder();

  typedef NMMSS::NV4L2::MMapStreamingContext source_context_type;
  typedef source_context_type::bound_allocator_type allocator_type;

  bool Open(DECLARE_LOGGER_ARG, NMMSS::NV4L2::FormatHints const &hints, NMMSS::ISample& firstSampleToLaunchDecodingPipe, allocator_type& allocator, bool scalingIsRequired);
  bool ChangeOutputResolution(DECLARE_LOGGER_ARG, unsigned width, unsigned height, allocator_type& allocator);

  void Dispose();

  enum EStatusFlags {
      EIGNORED = 0,
      ETRANSFORMED = 1 << 0,
      EDROPPED_INPUT = 1 << 1,
      EDROPPED_OUTPUT = 1 << 2,
      EFAILED = 1 << 3
  };
  typedef uint32_t Status;
  Status Decode(DECLARE_LOGGER_ARG, NMMSS::ISample&);
  NMMSS::ISample* GetPicture(allocator_type& allocator);
  NMMSS::ISample* CopyPicture(NMMSS::IAllocator& allocator);

  void SetDropMode() { m_bDropPictures = true; }
  void ClearDropMode() { m_bDropPictures = false; }
  static constexpr bool IsMJPEGSupported() { return false; }

protected:
  bool OpenMFCDecoder(DECLARE_LOGGER_ARG);
  bool CheckMFCDecoderFormats(DECLARE_LOGGER_ARG);
  bool OpenConverter(DECLARE_LOGGER_ARG);
  void SetupConverter(DECLARE_LOGGER_ARG, unsigned width, unsigned height, allocator_type& allocator);

  NMMSS::NV4L2::CV4L2Device m_decoderDev;
  NMMSS::NV4L2::CV4L2Device m_converterDev;

  NMMSS::NV4L2::MMapStreamingContext* m_decoderSink;
  NMMSS::NV4L2::MMapStreamingContext* m_decoderSource;
  NMMSS::NV4L2::UserPtrStreamingContext* m_converterSink;
  source_context_type* m_converterSource;

  bool m_bDropPictures;
  bool m_hasTargetFormatSupport;
  bool m_mfcFormatsChecked;
  bool m_needConverter;
  NMMSS::NV4L2::buf_index_type m_freeBuffersCount;
  NMMSS::NV4L2::buf_index_type m_iDequeuedToPresentBufferNumber;
};

} //namespace NExynos

#endif //_MMCODING_VIDEO_CODEC_EXYNOS_H

