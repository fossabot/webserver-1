#ifndef ITVSDKUTIL_EVENTARGSADJUSTER_H
#define ITVSDKUTIL_EVENTARGSADJUSTER_H

#include "BaseEventArgsAdjuster.h"

class EventArgsAdjuster : public BaseEventArgsAdjuster
{
public:
    EventArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix, NMMSS::IDetectorEvent* event);

protected:
    virtual ITV8::hresult_t SetRectangleArray(const std::string& name, ITV8::IRectangleEnumerator* collection);

private:
    ITV8::uint32_t m_contourId;
};

#endif // ITVSDKUTIL_EVENTARGSADJUSTER_H
