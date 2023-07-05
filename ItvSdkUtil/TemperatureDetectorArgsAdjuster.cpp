#include "TemperatureDetectorArgsAdjuster.h"

#include <vector>

#include <ItvDetectorSdk/include/DetectorConstants.h>
#include <ItvSdk/include/VisualPrimitives.h>
#include <ItvFramework/TimeConverter.h>

#include <MMIDL/MMVideoC.h>

TemperatureDetectorArgsAdjuster::TemperatureDetectorArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix,
                                                   NMMSS::IDetectorEvent* event)
    : BaseEventArgsAdjuster(GET_LOGGER_PTR, prefix, event)
{
}

ITV8::hresult_t TemperatureDetectorArgsAdjuster::SetMultimediaBuffer(const std::string& name,
                                                         boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer)
{
    ITV8::IFloatMatrix* const temperatureMatrix = 
        ITV8::contract_cast<ITV8::IFloatMatrix>(multimediaBuffer.get());

    if (!temperatureMatrix)
    {
        return BaseEventArgsAdjuster::SetMultimediaBuffer(name, multimediaBuffer);
    }

    auto size = temperatureMatrix->GetWidth() * temperatureMatrix->GetHeight();
    auto data = temperatureMatrix->GetData();

    auto min_max = std::minmax_element(data, data + size);

    _inf_ << "TemperatureMatrixMinMax: " << *min_max.first << " - " << *min_max.second;

    return processMatrix(name, temperatureMatrix);
}

ITV8::hresult_t TemperatureDetectorArgsAdjuster::processMatrix(const std::string& name, const ITV8::IFloatMatrix* matrix)
{
    if (!matrix)
    {
        return ITV8::EInvalidParams;
    }

    auto width = static_cast<int32_t>(matrix->GetWidth());
    auto height = static_cast<int32_t>(matrix->GetHeight());

    event()->SetValue(name, matrix->GetData(), width * height);

    event()->SetValue("Width", width);
    event()->SetValue("Height", height);

    return ITV8::ENotError;
}
