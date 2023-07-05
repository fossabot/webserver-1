#ifndef MMCODING_TRANSFORMS_H_
#define MMCODING_TRANSFORMS_H_

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4244)
#endif

#include <CorbaHelpers/GccUtils.h>

GCC_SUPPRESS_WARNING_BEGIN((-Wmisleading-indentation))

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/multi/geometries/multi_polygon.hpp>

GCC_SUPPRESS_WARNING_END();

#ifdef _WIN32
#pragma warning(pop)
#endif

#include <boost/shared_array.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "../MMSS.h"
#include "../mIntTypes.h"
#include "../MediaType.h"
#include "MMCodingExports.h"
#include "Initialization.h"
#include "FrameGeometryAdvisor.h"

namespace NMMSS
{
MMCODING_DECLSPEC IFilter* CreateVideoDecoderPullFilter(DECLARE_LOGGER_ARG,
                                                               IFrameGeometryAdvisor* advisor=0, bool multithreaded = false);

typedef boost::geometry::model::d2::point_xy<double>    Point;
typedef boost::geometry::model::polygon<Point>          Polygon;
typedef boost::geometry::model::box<Point>              Box;

typedef std::list<Box>                                         TBoxList;
typedef std::list<Polygon>                                     TPolygonList;
typedef std::pair<boost::posix_time::time_period, Polygon>     TTimeMask;
typedef std::list<TTimeMask>                                   TTimeMaskList;
typedef std::pair<boost::posix_time::time_period, std::string> TTimeComment;
typedef std::vector<TTimeComment>                              TTimeCommentList;

enum EVideoCodingPreset
{
    VCP_BestQuality =0
,   VCP_FineQuality =1
,   VCP_GoodQuality =2
,   VCP_Normal      =3
,   VCP_SmallSize   =4
,   VCP_TinySize    =5
,   VCP_BestSize    =6
};

enum EPassableSample { PS_EVERY_SECOND=2, PS_EVERY_FOURTH=4, PS_EVERY_EIGHT=8 };

MMCODING_DECLSPEC IFilter* CreateMPEG4EncoderFilter(DECLARE_LOGGER_ARG, EVideoCodingPreset quality = VCP_Normal);
MMCODING_DECLSPEC IFilter* CreateH264EncoderFilter(DECLARE_LOGGER_ARG, EVideoCodingPreset quality = VCP_Normal);
MMCODING_DECLSPEC IFilter* CreateMJPEGEncoderFilter(DECLARE_LOGGER_ARG, EVideoCodingPreset quality = VCP_Normal);

MMCODING_DECLSPEC IFilter* CreateAudioResamplerPullFilter(
    DECLARE_LOGGER_ARG,
    unsigned int nSampleRate,
    unsigned int nChannelsCount,
    NMMSS::NMediaType::Audio::ESampleType nType);

MMCODING_DECLSPEC IFilter* CreateAudioDecoderPullFilter(DECLARE_LOGGER_ARG);

MMCODING_DECLSPEC IFilter* CreateAudioEncoderFilter_G711_A(DECLARE_LOGGER_ARG);
MMCODING_DECLSPEC IFilter* CreateAudioEncoderFilter_G711_U(DECLARE_LOGGER_ARG);
MMCODING_DECLSPEC IFilter* CreateAudioEncoderFilter_G726(DECLARE_LOGGER_ARG, int bitrate);
MMCODING_DECLSPEC IFilter* CreateAudioEncoderFilter_AAC(DECLARE_LOGGER_ARG, int compression);
MMCODING_DECLSPEC IFilter* CreateAudioEncoderFilter_VORBIS(DECLARE_LOGGER_ARG, int compression);
MMCODING_DECLSPEC IFilter* CreateAudioEncoderFilter_MP2(DECLARE_LOGGER_ARG, int compression);

MMCODING_CLASS_DECLSPEC uint8_t CalculateScalingShift(uint32_t requested, uint32_t actual);
MMCODING_DECLSPEC IFilter* CreateScaleFilter(DECLARE_LOGGER_ARG, uint32_t width, uint32_t height, bool storeAspectRatio);
MMCODING_DECLSPEC IFilter* CreateAreaFilter(DECLARE_LOGGER_ARG, float x1, float y1, float x2, float y2); // top-left origin
MMCODING_DECLSPEC IFilter* CreateSieveFilter(DECLARE_LOGGER_ARG, EPassableSample sieve);
MMCODING_DECLSPEC IFilter* CreateWatermarkFilter(DECLARE_LOGGER_ARG, const std::wstring& watermark, float opacity, float x1, float y1, float x2, float y2); // top-left origin
MMCODING_DECLSPEC IFilter* CreateBurnSubtitleFilter(DECLARE_LOGGER_ARG,
                                                    const std::wstring& timeFormat,
                                                    const boost::posix_time::minutes& timezone,
                                                    const std::wstring& comment,
                                                    const TTimeCommentList& timeComments,
                                                    const std::wstring& fontPath,
                                                    int fontIndex,
                                                    int fontSize,
                                                    int fontStyle, // NORMAL(0x00) | BOLD(0x01) | ITALIC(0x02) | UNDERLINE(0x04) | STRIKETHROUGH(0x08)
                                                    unsigned char color[3],
                                                    const std::string& preferredLocale);

struct ISampleReader : public virtual NCorbaHelpers::IRefcounted
{
    virtual PSample PullSample(bool wait) = 0;
};

enum EBurnMode { LIVE_STREAM, ARCHIVE_STREAM, LIVE_SNAPSHOT, ARCHIVE_SNAPSHOT };

MMCODING_DECLSPEC IFilter* CreateBurnTextFilter(DECLARE_LOGGER_ARG,
                                                ISampleReader* textSource,
                                                const std::wstring& fontPath,
                                                int fontIndex,
                                                int fontSize,
                                                int fontStyle, // NORMAL(0x00) | BOLD(0x01) | ITALIC(0x02) | UNDERLINE(0x04) | STRIKETHROUGH(0x08)
                                                unsigned char color[4],
                                                float x1, float y1, float x2, float y2,
                                                EBurnMode mode, uint64_t beginTime, uint64_t endTime);

enum ECameraPlace { CEILING, WALL, GROUND };
enum EDewarpMode { PTZ, PERIMETER };

MMCODING_DECLSPEC IFilter* CreateFisheyeDewarpFilter(DECLARE_LOGGER_ARG,
                                                     float fisheyeLeft,
                                                     float fisheyeRight,
                                                     float fisheyeTop,
                                                     float fisheyeBottom,
                                                     float fov,
                                                     float pan,
                                                     float tilt,
                                                     float zoom,
                                                     uint32_t destW,
                                                     uint32_t destH,
                                                     ECameraPlace place,
                                                     EDewarpMode mode);

#ifdef WIN32
MMCODING_DECLSPEC IFilter* CreateImmerVisionDewarpFilter(DECLARE_LOGGER_ARG,
                                                         float pan,
                                                         float tilt,
                                                         float zoom,
                                                         const char* lens,
                                                         ECameraPlace place,
                                                         EDewarpMode mode,
                                                         uint32_t destW,
                                                         uint32_t destH);
#endif

MMCODING_DECLSPEC void CutOutImageArea(const std::string& src, const std::string& dst, float x1, float y1, float x2, float y2);


//all crop_ parameters: ratio
MMCODING_DECLSPEC IFilter* CreateSizeFilter(DECLARE_LOGGER_ARG, uint32_t width, uint32_t height
                                             , bool crop = false, float crop_x = 0.f, float crop_y = 0.f, float crop_width = 1.f, float crop_height = 1.f);

struct ITrafficTuner : public virtual NCorbaHelpers::IRefcounted
{
    virtual void SetTrafficLimit(size_t bytesPerSecond) = 0;
    virtual IFilter* CreateChannel(const std::string& id, const NMMSS::SAllocatorRequirements& req) = 0;
};
typedef NCorbaHelpers::CAutoPtr<ITrafficTuner> PTrafficTuner;

MMCODING_DECLSPEC ITrafficTuner* CreateTrafficTuner(DECLARE_LOGGER_ARG, size_t bytesPerSecond, size_t tunePeriod);

struct IOverlayProvider : public virtual NCorbaHelpers::IRefcounted
{
    virtual TBoxList Advance(const boost::posix_time::ptime& time) = 0;
};
typedef NCorbaHelpers::CAutoPtr<IOverlayProvider> POverlayProvider;

MMCODING_DECLSPEC IOverlayProvider* CreateTrackOverlayProvider(DECLARE_LOGGER_ARG,
                                                               NMMSS::IPullStyleSource* vmda,
                                                               const boost::posix_time::ptime& end = boost::date_time::max_date_time,
                                                               const boost::posix_time::time_duration& wait = boost::date_time::not_a_date_time);

MMCODING_DECLSPEC IFilter* CreateOverlayFilter(DECLARE_LOGGER_ARG, POverlayProvider provider, unsigned char color[4]);

typedef std::tuple<boost::shared_array<unsigned char>, uint32_t, uint32_t> TPixelMask;

struct IPixelMaskProvider : public virtual NCorbaHelpers::IRefcounted
{
    virtual TPixelMask Advance(const boost::posix_time::ptime& time) = 0;
};
typedef NCorbaHelpers::CAutoPtr<IPixelMaskProvider> PPixelMaskProvider;

MMCODING_DECLSPEC IPixelMaskProvider* CreatePixelMaskProvider(DECLARE_LOGGER_ARG,
                                                              NMMSS::IPullStyleSource* mask,
                                                              const boost::posix_time::ptime& end = boost::date_time::max_date_time,
                                                              const boost::posix_time::time_duration& wait = boost::date_time::not_a_date_time);

MMCODING_DECLSPEC IPixelMaskProvider* CreateTrackMaskProvider(DECLARE_LOGGER_ARG,
                                                              NMMSS::IPullStyleSource* vmda,
                                                              const boost::posix_time::ptime& end = boost::date_time::max_date_time,
                                                              const boost::posix_time::time_duration& wait = boost::date_time::not_a_date_time);

MMCODING_DECLSPEC IPixelMaskProvider* CreateShapeMaskProvider(DECLARE_LOGGER_ARG,
                                                              const TPolygonList& staticMask,
                                                              const TTimeMaskList& timeMask);

MMCODING_DECLSPEC IFilter* CreatePixelMaskFilter(DECLARE_LOGGER_ARG, PPixelMaskProvider provider, bool invert = false);

MMCODING_DECLSPEC IFilter* CreateRuntimePixelMaskFilter(DECLARE_LOGGER_ARG, IFilter* producer, bool invert = false);

MMCODING_DECLSPEC IFilter* CreateRuntimeTrackMaskFilter(DECLARE_LOGGER_ARG, IFilter* producer, bool invert = false);

}

#endif
