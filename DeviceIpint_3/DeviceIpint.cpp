#include <ItvSdkWrapper.h>
#include "DeviceIpint.h"
#include "IIPManager3.h"
#include "CIpInt30.h"

namespace IPINT30
{
    DEVICEIPINT_CLASS_DECLSPEC IPINT30::IIpintDevice* CreateIPINT30(DECLARE_LOGGER_ARG, const char* szObjectID, NCorbaHelpers::IContainerNamed* pContainer,
        bool callbackNeeded, boost::shared_ptr<SDeviceSettings> settingsPtr, NExecutors::PDynamicThreadPool threadPool)
    {
        return new IPINT30::CDevice(GET_LOGGER_PTR, szObjectID, pContainer, callbackNeeded, settingsPtr, threadPool);
    }
}
