#include "CommonUtility.h"


namespace bl = axxonsoft::bl;

namespace
{
    class SortedVec
    {
    public:
        SortedVec(const std::vector<std::string> &vs_) :
            vs(vs_)
        {
            std::sort(vs.begin(), vs.end());
        }

        bool operator< (const SortedVec &a) const
        {
            if (vs.size() != a.vs.size())
                return vs.size() < a.vs.size();

            for (size_t i = 0; i < vs.size(); ++i)
                if (vs[i] != a.vs[i])
                    return vs[i] < a.vs[i];

            return false;
        }
    public:
        std::vector<std::string> vs;
    };


    const std::map<SortedVec, std::string> EVENT_TYPES_MAP =
    {
                { {{ "TargetList:DetectorPeriodicalEventType" }}, "SceneDescription"},
                { {{ "lotsObjects:DetectorTwoPhaseEventType" }},     "LotsObjects" },
                { {{ "AreaToArea:DetectorOnePhaseEventType" }},     "AreaToArea" },
                { {{ "outOfZone:DetectorOnePhaseEventType" }}, "OutOfZone"},
                { {{ "comeInZone:DetectorTwoPhaseEventType" }}, "ComeInZone"},
                { {{ "moveInZone:DetectorTwoPhaseEventType" }}, "MoveInZone" },
                { {{ "lostObject:DetectorOnePhaseEventType" }}, "LostObject" },
                { {{ "stopInZone:DetectorTwoPhaseEventType" }}, "StopInZone" },
                { {{ "longInZone:DetectorTwoPhaseEventType" }}, "LongInZone" },
                { {{ "oneLine:DetectorTwoPhaseEventType" }}, "CrossOneLine" },

                { {{ "HumanBoneTargetList:DetectorPeriodicalEventType" }}, "PoseDetector"},
                { {{ "sitDown:DetectorTwoPhaseEventType" }}, "SitDownDetector"},
                { {{ "recumbent:DetectorTwoPhaseEventType" }}, "RecumbentDetector"},
                { {{ "handsUp:DetectorTwoPhaseEventType" }}, "HandsUpDetector"},
                { {{ "activeShooter:DetectorTwoPhaseEventType" }}, "ActiveShooterDetector" },
                { {{ "NoHandOnRail:DetectorTwoPhaseEventType" }}, "HandRailDetector" },
                { {{ "lotsObjects:DetectorOnePhaseEventType", "ObjectCountMask:DetectorPeriodicalEventType" }}, "PeopleCountDetectorBySkeleton" },
                { {{ "peopleDistance:DetectorTwoPhaseEventType" }}, "PeopleDistanceDetector" },
                { {{ "PrivateMask:DetectorPeriodicalEventType" }}, "PoseMaskingDetector" },

                { {{ "SceneChangeDetected:DetectorOnePhaseEventType" }}, "SceneChange" },
                { {{ "CameraBlindDetected:DetectorOnePhaseEventType" }}, "QualityDegradation" },
                { {{ "ImageDegradation:DetectorTwoPhaseEventType" }}, "QualityDegradation_v2" },
                { {{ "BlurredDegradation:DetectorTwoPhaseEventType" }}, "BlurredDegradation" },
                { {{ "CompressedDegradation:DetectorTwoPhaseEventType" }}, "CompressedDegradation" },
                { {{ "MotionDetected:DetectorTwoPhaseEventType", "MotionMask:DetectorPeriodicalEventType", "TargetList:DetectorPeriodicalEventType" }}, "MotionDetection" },
                { {{ "NullAudio:DetectorTwoPhaseEventType" }}, "NullAudioDetection" },
                { {{ "NoiseAudio:DetectorTwoPhaseEventType" }}, "NoiseAudioDetection"},
                { {{ "SignalAudio:DetectorTwoPhaseEventType" }}, "SignalAudioDetection"},

                { {{ "FireDetected:DetectorTwoPhaseEventType", "SmokeMask:DetectorPeriodicalEventType" }}, "FireDetector" },
                { {{ "TargetList:DetectorPeriodicalEventType", "plateRecognized:DetectorOnePhaseEventType" }}, "LprDetector_Vit" },
                { {{ "lotsObjects:DetectorOnePhaseEventType", "BinaryMask:DetectorPeriodicalEventType", "ObjectCountMask:DetectorPeriodicalEventType"}}, "NeuroCounter" },
                { {{ "TargetList:DetectorPeriodicalEventType" }}, "NeuroTracker"},
                { {{ "ObjectDescriptorEvent:DetectorOnePhaseEventType" }}, "NeuroFeatureExtractor" },
                { {{ "ObjectDescriptorEvent:DetectorOnePhaseEventType" }}, "HumanRecognitionDetector" },

                { {{ "QueueDetected:DetectorOnePhaseEventType", "QueueMask:DetectorPeriodicalEventType" }}, "QueueDetector" },
                { {{ "PeopleIn:DetectorOnePhaseEventType", "PeopleOut:DetectorOnePhaseEventType" }}, "PeopleCounter" },

                { {{ "SmokeDetected:DetectorTwoPhaseEventType", "SmokeMask:DetectorPeriodicalEventType" }}, "SmokeDetector"},
                { {{ "TargetList:DetectorPeriodicalEventType", "faceAppeared:DetectorOnePhaseEventType" }}, "TvaFaceDetector" },
                { {{ "EvasionDetected:DetectorTwoPhaseEventType" }}, "FaceEvasionDetector"},
                { {{ "LevelOut:DetectorTwoPhaseEventType", "WaterLevelMask:DetectorPeriodicalEventType" }}, "WaterLevelDetector" },

                { {{ "PrivateMask:DetectorPeriodicalEventType" }}, "FaceMaskingDetector" },

                { {{ "NoDetectSegment:DetectorTwoPhaseEventType", "ColorMask:DetectorPeriodicalEventType" }}, "MaskSegmentsDetector" },
                { {{ "PrivateMask:DetectorPeriodicalEventType" }}, "BlurSegmentsDetector" },
                { {{ "NoHeadSegment:DetectorOnePhaseEventType",
                     "NoHandSegment:DetectorOnePhaseEventType",
                     "NoBodySegment:DetectorOnePhaseEventType",
                     "NoHipsSegment:DetectorOnePhaseEventType",
                     "NoFootSegment:DetectorOnePhaseEventType",
                     "NoOtherSegment:DetectorOnePhaseEventType",
                     "NoAllSegment:DetectorOnePhaseEventType",
                     "ColorMask:DetectorPeriodicalEventType" }},
                  "SelfMaskSegmentDetector" },

                // Embedded detectors
                { {{ "motion:DetectorTwoPhaseEventType" }}, "MotionDetector" },
                { {{ "plateRecognized:DetectorOnePhaseEventType" }}, "LprDetector" },
                { {{ "bodyTemperature:DetectorOnePhaseEventType", "bodyPreTemperature:DetectorOnePhaseEventType" }}, "ThermometryBodyDetection" },
                { {{ "temparatureEvent:DetectorTwoPhaseEventType" }}, "TemperatureDetection" }
    };


    static std::string convert(const bl::domain::DetectorEventInfo::EventType &type)
    {
        switch (type)
        {
        case bl::domain::DetectorEventInfo_EventType_ONE_PHASE_EVENT_TYPE:
            return "DetectorOnePhaseEventType";
        case bl::domain::DetectorEventInfo_EventType_TWO_PHASE_EVENT_TYPE:
            return "DetectorTwoPhaseEventType";
        case bl::domain::DetectorEventInfo_EventType_PERIODICAL_EVENT_TYPE:
            return "DetectorPeriodicalEventType";
        default:
            return "";
        };

        return "";
    }
}

namespace NPluginUtility
{


    //TODO: deprecate this. Get event type from .proto
    std::string Convert(const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::DetectorEventInfo > &events)
    {
        std::vector<std::string> vs;
        int itemCount = events.size();
        for (int i = 0; i < itemCount; ++i)
        {
            const auto &detectorInfo = events.Get(i);
            vs.push_back(detectorInfo.id() + ":" + convert(detectorInfo.event_type()));
        }

        auto it = EVENT_TYPES_MAP.find(SortedVec(vs));
        if (it != EVENT_TYPES_MAP.end())
            return it->second;

        return "unknown";
    }
}