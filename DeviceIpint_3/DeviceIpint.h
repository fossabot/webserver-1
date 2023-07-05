#ifndef DEVICEIPINT_H
#define DEVICEIPINT_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.
#include <boost/asio.hpp>
//
#include "IIPManager3.h"
#include "DeviceIpint_Exports.h"
#include <Executors/DynamicThreadPool.h>

namespace NMMSS
{
    class IGrabberCallback;
}

namespace NCorbaHelpers
{
    class IContainerNamed;
}

namespace IPINT30
{
    struct SDeviceSettings;

    DEVICEIPINT_CLASS_DECLSPEC IIpintDevice* CreateIPINT30(DECLARE_LOGGER_ARG,
        const char* szObjectID, NCorbaHelpers::IContainerNamed* pContainer,
        bool callbackNeeded, boost::shared_ptr<SDeviceSettings> settingsPtr,
        NExecutors::PDynamicThreadPool threadPool);
}


#endif // DEVICEIPINT_H
