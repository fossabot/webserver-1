#ifndef MMCODING_TWEAKABLEFILTER_H_
#define MMCODING_TWEAKABLEFILTER_H_

#include "MMCodingExports.h"
#include "../Augments.h"
#include "../MMSS.h"
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <chrono>

namespace NMMSS {

    class ITweakableFilter
        : public virtual IFilter
        , public virtual ITweakable
    {};
    using PTweakableFilter = NCorbaHelpers::CAutoPtr<ITweakableFilter>;

    MMCODING_DECLSPEC ITweakableFilter* CreateTweakableFilter(DECLARE_LOGGER_ARG, CAugment const&);

    namespace NAugment {

        ////////////////////////////////////////////////////////////////////////////////

        struct Decimation
        {
            std::chrono::milliseconds period;
            bool onlyKeyFrames;
            bool markPreroll = false;
        };

        inline bool operator==( Decimation const& lhs, Decimation const& rhs )
        { return lhs.period == rhs.period && lhs.onlyKeyFrames == rhs.onlyKeyFrames && lhs.markPreroll == rhs.markPreroll; }
        inline bool operator<( Decimation const& lhs, Decimation const& rhs )
        { return lhs.period < rhs.period || 
                (lhs.period == rhs.period && lhs.onlyKeyFrames < rhs.onlyKeyFrames) ||
                (lhs.period == rhs.period && lhs.onlyKeyFrames == rhs.onlyKeyFrames && lhs.markPreroll < rhs.markPreroll); }

        ////////////////////////////////////////////////////////////////////////////////

        struct Buffer
        {
            std::chrono::milliseconds length;
            boost::posix_time::ptime start;
            bool markDiscontinuty = false;
        };

        inline bool operator==( Buffer const& lhs, Buffer const& rhs )
        { return lhs.length==rhs.length && lhs.start==rhs.start && lhs.markDiscontinuty==rhs.markDiscontinuty; }
        inline bool operator<( Buffer const& lhs, Buffer const& rhs )
        { return lhs.length<rhs.length || (lhs.length==rhs.length && (lhs.start<rhs.start || (lhs.start==rhs.start && lhs.markDiscontinuty<rhs.markDiscontinuty))); }

        ////////////////////////////////////////////////////////////////////////////////

        struct BIMWDownscale
        {
            uint32_t width;
            uint32_t height;
        };

        inline bool operator==( BIMWDownscale const& lhs, BIMWDownscale const& rhs )
        { return lhs.width==rhs.width && lhs.height==rhs.height; }
        inline bool operator<( BIMWDownscale const& lhs, BIMWDownscale const& rhs )
        { return lhs.width<rhs.width || (lhs.width==rhs.width && lhs.height<rhs.height); }

        ////////////////////////////////////////////////////////////////////////////////

    } // namespace NAugment

} // namespace NMMSS

#endif // MMCODING_TWEAKABLEFILTER_H_
