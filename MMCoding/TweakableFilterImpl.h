#ifndef MMCODING_TWEAKABLEFILTERIMPL_H_
#define MMCODING_TWEAKABLEFILTERIMPL_H_

#include "TweakableFilter.h"
#include "../FilterImpl.h"

namespace NMMSS {

    template<class TTransform>
    class CTweakableFilterImpl
        : public virtual ITweakableFilter
        , public CPullFilterImpl<TTransform>
    {
    public:
        CTweakableFilterImpl(DECLARE_LOGGER_ARG, TTransform* transform,
            SAllocatorRequirements outputReq = SAllocatorRequirements(),
            SAllocatorRequirements inputReq = SAllocatorRequirements()
        )
            : CPullFilterImpl<TTransform>(GET_LOGGER_PTR, outputReq, inputReq, transform)
        {
        }
        void Tweak(CAugment const& aug) override
        {
            boost::unique_lock<boost::mutex> lock(this->m_transformMutex);
            this->m_transform->Tweak(aug);
        }
        CAugment GetTweak() const override
        {
            boost::unique_lock<boost::mutex> lock(this->m_transformMutex);
            return this->m_transform->GetTweak();
        }
    };

} // namespace NMMSS

#endif // MMCODING_TWEAKABLEFILTERIMPL_H_
