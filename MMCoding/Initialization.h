#ifndef MMSS_MMCONDING_INITIALIZATION_H_
#define MMSS_MMCONDING_INITIALIZATION_H_

#include <stdexcept>
#include <Logging/log2.h>
#include "MMCodingExports.h"
#include <CorbaHelpers/Refcounted.h>

namespace NMMSS
{

class MMCODING_CLASS_DECLSPEC CMMCodingInitialization
{
public:
    CMMCodingInitialization(DECLARE_LOGGER_ARG);
    ~CMMCodingInitialization();
private:
    NCorbaHelpers::IRefcounted* m_initImpl;
};



namespace details
{
    MMCODING_EXPORT_DECLSPEC NCorbaHelpers::IRefcounted* GetShareableMMCodingInitializationImpl(DECLARE_LOGGER_ARG);
}

inline NCorbaHelpers::PRefcounted GetShareableMMCodingInitialization(DECLARE_LOGGER_ARG)
{
    return NCorbaHelpers::PRefcounted(details::GetShareableMMCodingInitializationImpl(GET_LOGGER_PTR));
}

}

#endif
