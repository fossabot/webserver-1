#pragma once

#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include "../MMClient/DetectorEventFactory.h"

#include "BaseEventArgsAdjuster.h"

class CMaskEventDataAdjuster : public BaseEventArgsAdjuster
{
    enum EMaskType
    {
        EMT_MotionMask = 0,
        EMT_ColorMask = 1,
        // Used to hide personal information
        // The values of cells are:
        // 0 - normal cell, no sensitive information
        // 1 - the contents of the cell should always be hidden
        // 2 - the contents of the cell should be hidden unless the user has special access
        EMT_PrivateMask = 2,
        // A simple mask. Currently used in NeuroCounter to show detected objects
        // The values of cells are:
        // 0 - unmasked cell
        // 1 - masked cell
        EMT_BinaryMask = 3,
        EMT_SmokeMask = 4,
        EMT_QueueMask = 5,
        EMT_WaterLevelMask = 6,
        EMT_ObjectCountMask = 7,
    };

    const static uint8_t MOTION_MASK_ITEM = 1;
    const static uint8_t COLOR_MASK_ITEM = 3;

    uint8_t m_bytesPerItem;
    int32_t m_maskType;
    uint32_t m_additionalBytes;
public:
    CMaskEventDataAdjuster(DECLARE_LOGGER_ARG,
                           const std::string& prefix,
                           NMMSS::IDetectorEvent* event)
        : BaseEventArgsAdjuster(GET_LOGGER_PTR, prefix, event)
    {
        m_bytesPerItem = 1;
        m_maskType = 0;
        m_additionalBytes = 0;

        if (prefix == "MotionMask")
        {
            m_bytesPerItem = 1;
            m_maskType = static_cast<int32_t>(EMT_MotionMask);
            m_additionalBytes = 2;
        }
        else if (prefix == "ColorMask")
        {
            m_bytesPerItem = 3;
            m_maskType = static_cast<int32_t>(EMT_ColorMask);
        }
        else if (prefix == "PrivateMask")
        {
            m_bytesPerItem = 1;
            m_maskType = static_cast<int32_t>(EMT_PrivateMask);
        }
        else if (prefix == "BinaryMask")
        {
            m_bytesPerItem = 1;
            m_maskType = static_cast<int32_t>(EMT_BinaryMask);
        }
        else if (prefix == "SmokeMask")
        {
            m_bytesPerItem = 0;
            m_maskType = static_cast<int32_t>(EMT_SmokeMask);
            m_additionalBytes = 3;
        }
        else if (prefix == "QueueMask")
        {
            m_bytesPerItem = 1;
            m_maskType = static_cast<int32_t>(EMT_QueueMask);
            m_additionalBytes = 8;
        }
        else if (prefix == "WaterLevelMask")
        {
            m_bytesPerItem = 0;
            m_maskType = static_cast<int32_t>(EMT_WaterLevelMask);
            m_additionalBytes = 12;
        }
        else if (prefix == "ObjectCountMask")
        {
            m_bytesPerItem = 0;
            m_maskType = static_cast<int32_t>(EMT_ObjectCountMask);
            m_additionalBytes = 4;
        }
    }

public:
    virtual ITV8::hresult_t SetMask(const std::string& name, ITV8::IMask* mask)
    {
        event()->SetValue("maskType", m_maskType);
        event()->SetValue("Height", mask->GetSize().height);
        event()->SetValue("Width", mask->GetSize().width);
        const auto bytesCount = (mask->GetSize().height) * (mask->GetSize().width) * m_bytesPerItem + m_additionalBytes;
        event()->SetValue(name, mask->GetMask(), bytesCount);
        return ITV8::ENotError;
    }
};
