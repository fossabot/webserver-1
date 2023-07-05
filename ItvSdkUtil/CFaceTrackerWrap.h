#ifndef ITVSDKUTIL_CFACETRACKERWRAP_H
#define ITVSDKUTIL_CFACETRACKERWRAP_H

#include <boost/shared_ptr.hpp>

#include <ItvSdk/include/VisualPrimitives.h>

#include "../PtimeFromQword.h"

#include "FaceTracker.h"

class CFaceTrackerWrap
{
public:
    typedef std::map<ITV8::uint64_t, ITV8::RectangleF> TTrackMap;

public:
    CFaceTrackerWrap()
        : m_faceTracker(CreateTracker())
        , m_initTime(boost::posix_time::not_a_date_time)
    {}
    void OnFrame(ITV8::timestamp_t frameTime, const std::vector<ITV8::RectangleF>& rects)
    {
        boost::posix_time::ptime ft = NMMSS::PtimeFromQword(frameTime);
        if (m_initTime.is_not_a_date_time())
            m_initTime = ft;
        ITV8::timestamp_t deltaTime = (ft - m_initTime).total_milliseconds();
        m_faceTracker->OnFrame(deltaTime, rects);
    }
    void GetCurrentTracks(TTrackMap& appTracks, TTrackMap& allTracks, TTrackMap& disappTracks)
    {
        m_faceTracker->GetCurrentTracks(appTracks, allTracks, disappTracks);
    }
    void ForceFinishTracks(TTrackMap& disappTracks)
    {
        m_faceTracker->ForceFinishTracks(disappTracks);
    }

private:
    boost::shared_ptr<IFaceTracker> m_faceTracker;
    boost::posix_time::ptime m_initTime;

};

#endif // ITVSDKUTIL_CFACETRACKERWRAP_H
