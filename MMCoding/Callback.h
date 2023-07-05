#pragma once

#include <CorbaHelpers/Refcounted.h>

namespace NMMSS
{
    class ICallback : public virtual NCorbaHelpers::IRefcounted
    {
    public:
        virtual void Call() = 0;
    };

    using PCallback = NCorbaHelpers::CAutoPtr<ICallback>;
}
