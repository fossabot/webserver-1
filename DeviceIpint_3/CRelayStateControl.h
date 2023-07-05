#ifndef DEVICEIPINT3_CRELAYSTATECONTROL_H
#define DEVICEIPINT3_CRELAYSTATECONTROL_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.
#include <CorbaHelpers/RefcountedImpl.h>

#include <ItvDeviceSdk/include/ItvDeviceSdk.h>
#include <ItvDeviceSdk/include/infoWriters.h>
#include "../../mmss/DeviceInfo/include/PropertyContainers.h"

#include "../Grabber/Grabber.h"
#include <CommonNotificationCpp/StateControlImpl.h>

#include "IIPManager3.h"

namespace IPINT30
{
	
class CRelayStateControl : public virtual NCommonNotification::CLegacyStateControl
{
public:
    CRelayStateControl(const char* szObjectId, NCommonNotification::PEventSupplier eventSink, bool state,
        unsigned short contact, IPINT30::IIOPanel* panel, DECLARE_LOGGER_ARG)
        :   m_contact(contact), m_ioPanel(panel, NCorbaHelpers::ShareOwnership())
    {
        Init(GET_LOGGER_PTR, szObjectId, this, eventSink,
            state, NStatisticsAggregator::PStatisticsAggregator());
    }

	virtual void DoStart()
	{
		//на первый взгляд неправильно что EIO_Closed, но суть в правильности переходов в StateControlImpl
		if (0 != m_ioPanel.Get())
			m_ioPanel->SetRelayState(m_contact, IPINT30::EIO_Closed);
	}

	virtual void DoStop()
	{
		if (0 != m_ioPanel.Get())
			m_ioPanel->SetRelayState(m_contact, IPINT30::EIO_Open);
	}

private:
	unsigned short m_contact;
	NCorbaHelpers::CAutoPtr<IPINT30::IIOPanel> m_ioPanel;
};

}//namespace IPINT30

#endif // DEVICEIPINT3_CRELAYSTATECONTROL_H