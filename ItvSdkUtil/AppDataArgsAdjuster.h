#ifndef ITVSDKUTIL_APPDATAARGSADJUSTER_H
#define ITVSDKUTIL_APPDATAARGSADJUSTER_H

#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include "../MMClient/DetectorEventFactory.h"

#include "BaseEventArgsAdjuster.h"

class CFaceTrackerWrap;

class AppDataArgsAdjuster : public BaseEventArgsAdjuster
{
public:
    AppDataArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix, NMMSS::IDetectorEvent* event,
        NMMSS::PDetectorEventFactory factory,
        ITV8::timestamp_t time, boost::shared_ptr<CFaceTrackerWrap> faceTrackerWrap);

public:
    virtual ITV8::hresult_t SetMultimediaBuffer(const std::string& name, 
        boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer);

    virtual ITV8::hresult_t SetRectangleArray(const std::string& name, ITV8::IRectangleEnumerator* collection);
    virtual ITV8::hresult_t SetMask(const std::string& name, ITV8::IMask* mask);

protected:
    ITV8::hresult_t setRectangle(ITV8::uint32_t id, const ITV8::RectangleF& rectangle, NMMSS::IDetectorEvent* event = 0) override;

private:
    ITV8::hresult_t processTarget(const ITV8::Analytics::ITarget* target);
    ITV8::hresult_t processRectangle(const ITV8::IRectangle* rect, std::vector<ITV8::RectangleF>& rects);
    void onFaceAppeared(ITV8::uint32_t id, const ITV8::RectangleF& faceRect, const char* uniqueFaceId = 0 /*uuid*/);
    ITV8::RectangleF transformRectangle(const ITV8::RectangleF& inRectangle);

private:
    NMMSS::PDetectorEventFactory m_factory;
    ITV8::timestamp_t m_time;
    boost::shared_ptr<CFaceTrackerWrap> m_faceTrackerWrap;

};

#endif // ITVSDKUTIL_APPDATAARGSADJUSTER_H
