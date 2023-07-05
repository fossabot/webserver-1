#include "EventArgsAdjuster.h"

#include <boost/bind.hpp>

EventArgsAdjuster::EventArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix,
                                     NMMSS::IDetectorEvent* event)
    : BaseEventArgsAdjuster(GET_LOGGER_PTR, prefix, event)
    , m_contourId(0)
{}

ITV8::hresult_t EventArgsAdjuster::SetRectangleArray(const std::string& name, 
                                                     ITV8::IRectangleEnumerator* collection)
{
    return iterateEnumerator(collection, [this](ITV8::IRectangle* rect)
        {
            return this->setRectangle(this->m_contourId++, rect->GetRectangle());
        });
}