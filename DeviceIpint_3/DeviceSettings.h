#ifndef DEVICEIPINT_3_DEVICESETTINGS_H
#define DEVICEIPINT_3_DEVICESETTINGS_H

#include <string>
#include <vector>
#include <map>
#include <boost/archive/xml_wiarchive.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
//#include <boost/serialization/map.hpp>
#include <boost/serialization/extended_type_info_typeid.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <InfraServer_IDL/ItvConfigurableImpl.h>
#include <ItvSdk/include/baseTypes.h>
#include "../Telemetry.h"

struct Mutable {};
struct NotMutable {};
struct Iterable {};
struct NotIterable {};

namespace boost {
    namespace serialization {

        template<class Archive>
        void serialize(Archive& ar, NMMSS::SPreset& preset, const unsigned int version)
        {
            ar  & boost::serialization::make_nvp("label", preset.label)
                & boost::serialization::make_nvp("savedOnDevice", preset.savedOnDevice)
                & boost::serialization::make_nvp("absolutePan", preset.position.pan)
                & boost::serialization::make_nvp("absoluteTilt", preset.position.tilt)
                & boost::serialization::make_nvp("absoluteZoom", preset.position.zoom);
        }

    } // namespace serialization
} // namespace boost

namespace IPINT30
{
const unsigned short LATEST_CONFIG_VERSION = 13;

struct SBase
{
protected:
    template<class TArchive>
    void serialize_optional(TArchive &ar, const std::string &name, std::string &value)
    {
        const std::string EMPTY_MARK = "-";
        std::string raw;
        if(typename TArchive::is_saving())
        {
            raw = value.empty() ? EMPTY_MARK : value;
            ar & boost::serialization::make_nvp(name.c_str(), raw);
        }
        else if(typename TArchive::is_loading())
        {
            ar & boost::serialization::make_nvp(name.c_str(), raw);
            value = (raw != EMPTY_MARK) ? raw : "";
        }
    }
};

struct SVideoStreamingParam
{
    // The flag indicates whether the stream can be enabled.
    int enabled;

    // The id of the video stream.
    int id;

    // The name of the stream.
    std::string name;

    // Dynamic video streaming params.
    NStructuredConfig::TCustomParameters publicParams;

    // New object parameters for future compactibility.
    NStructuredConfig::TCustomParameters metaParams;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int version)
    {
        ar  & boost::serialization::make_nvp("enabled", enabled);
        ar  & boost::serialization::make_nvp("id", id);
        ar  & boost::serialization::make_nvp("name", name);
        ar  & boost::serialization::make_nvp("publicParams", publicParams);
        if (ar.get_library_version() >= 7)
        {
            ar & boost::serialization::make_nvp("metaParams", metaParams);
        }
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SVideoStreamingParam> TVideoStreamingParams;

//TODO: Вынести общую часть конфигурации в базовый класс SChannel Param
struct SMicrophoneParam
{
    // The user friendly id.
    unsigned int displayId;

    // The id of the audio stream.
    int id;

    // The flag indicating whether the microphone is enabled.
    int enabled;

    // Dynamic microphone params.
    NStructuredConfig::TCustomParameters publicParams;

    // New object parameters for future compactibility.
    NStructuredConfig::TCustomParameters metaParams;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int version)
    {
        ar  & boost::serialization::make_nvp("displayId", displayId);
        ar  & boost::serialization::make_nvp("id", id);
        ar  & boost::serialization::make_nvp("enabled", enabled);
        ar  & boost::serialization::make_nvp("publicParams", publicParams);
        if (ar.get_library_version() >= 7)
        {
            ar & boost::serialization::make_nvp("metaParams", metaParams);
        }
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SMicrophoneParam> TMicrophoneParams;

struct SAudioDestinationParam
{
    // Display ID of the audio destination
    int displayId;

    // The id of the audio destination
    int id;

    // The flag indicating whether the audio destination enabled
    int enabled;

    // Dynamic audio destination params
    NStructuredConfig::TCustomParameters publicParams;

    // Private dynamic audio destination parameters.
    NStructuredConfig::TCustomParameters privateParams;

    // New object parameters for future compactibility.
    NStructuredConfig::TCustomParameters metaParams;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int version)
    {
        ar  & boost::serialization::make_nvp("displayId", displayId);
        ar  & boost::serialization::make_nvp("id", id);
        ar  & boost::serialization::make_nvp("enabled", enabled);
        ar  & boost::serialization::make_nvp("publicParams", publicParams);
        ar  & boost::serialization::make_nvp("privateParams", privateParams);
        if (ar.get_library_version() >= 7)
        {
            ar & boost::serialization::make_nvp("metaParams", metaParams);
        }
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SAudioDestinationParam> TAudioDestinationParams;

struct SEmbeddedStoragesParam
{
    // Display ID of the embedded storage
    int displayId;

    // The flag indicating whether the embedded storage enabled
    int enabled;

    // Dynamic embedded storage params
    NStructuredConfig::TCustomParameters publicParams;

    // Private dynamic embedded storage parameters.
    NStructuredConfig::TCustomParameters privateParams;

    // New object parameters for future compactibility.
    NStructuredConfig::TCustomParameters metaParams;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int version)
    {
        ar  & boost::serialization::make_nvp("displayId", displayId);
        ar  & boost::serialization::make_nvp("enabled", enabled);
        ar  & boost::serialization::make_nvp("publicParams", publicParams);
        ar  & boost::serialization::make_nvp("privateParams", privateParams);
        if (ar.get_library_version() >= 7)
        {
            ar & boost::serialization::make_nvp("metaParams", metaParams);
        }
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SEmbeddedStoragesParam> TEmbeddedStoragesParams;


struct SDecartPoint
{
    double x;
    double y;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar & boost::serialization::make_nvp("x", x);
        ar & boost::serialization::make_nvp("y", y);
    }
};

struct SPtzPoint
{
    long pan;
    long tilt;
    long zoom;
    long mask;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar & boost::serialization::make_nvp("pan", pan);
        ar & boost::serialization::make_nvp("tilt", tilt);
        ar & boost::serialization::make_nvp("zoom", zoom);
        ar & boost::serialization::make_nvp("mask", mask);
    }
};

struct SCalibrationPoint
{
    SDecartPoint dp;
    SPtzPoint pp;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar & boost::serialization::make_nvp("decart_point", dp);
        ar & boost::serialization::make_nvp("ptz_point", pp);
    }
};
typedef std::vector<SCalibrationPoint> TCalibrationParams;

struct STracker
{
    std::string corbaName;
    TCalibrationParams calibrationInfo;
    unsigned int predictionTime = 500;
    unsigned int timeBetweenPTZCommands = 1000;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar & boost::serialization::make_nvp("data_source", corbaName);
        ar & boost::serialization::make_nvp("calibration_info", calibrationInfo);

        if (ar.get_library_version() >= 8)
        {
            ar & boost::serialization::make_nvp("prediction_time", predictionTime);
            ar & boost::serialization::make_nvp("time_between_PTZ_commands", timeBetweenPTZCommands);
        }
    }
};
typedef std::vector<STracker> TTrackers;

struct SObserver
{
    int mode;
    int frequency;
    TTrackers trackers;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar & boost::serialization::make_nvp("mode", mode);
        ar & boost::serialization::make_nvp("frequency", frequency);
        ar & boost::serialization::make_nvp("trackers", trackers);
    }
};

typedef NMMSS::SPreset SPreset_t;

struct SCustomParamFindName
{
    const std::string& m_name;
    SCustomParamFindName(const std::string& name) : m_name(name) {}
    bool operator()(const NStructuredConfig::SCustomParameter& r) const { return (m_name == r.name); }
};

struct STelemetryParam
{
    // The user friendly id.
    unsigned int displayId;

    // The id of the telemetry.
    int id;

    // The flag indicating whether the telemetry is enabled.
    int enabled;

    // The flag indicating whether patrol feature should be enabled with telemetry enabling.
    int defaultEnablePatrol;

    // Timeout (sec) between switching presets in patrol mode.
    int patrolTimeout;

    // If enabled, PTZ presets will be saved on device,
    // otherwise NGP will use Absolute PTZ to save and recall presets.
    int savePresetsOnDevice;

    // Dynamic telemetry params.
    NStructuredConfig::TCustomParameters publicParams;

    // Read only dynamic telemetry params.
    NStructuredConfig::TCustomParameters readOnlyParams;

    // New object parameters for future compactibility.
    NStructuredConfig::TCustomParameters metaParams;

    // Presets info.
    // Key - preset position.
    // Value - user friendly preset label.
    typedef std::map<int /*pos*/, SPreset_t> TPresets;
    TPresets presets;

    SObserver observer;

    STelemetryParam()
        : defaultEnablePatrol(0)
        , patrolTimeout(1)
        , savePresetsOnDevice(0)
    {
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;

    //Gets int param.
    int GetIntParam(const char* param)const
    {
        using std::string;
        NStructuredConfig::TCustomParameters::const_iterator it =
        std::find_if(publicParams.begin(), publicParams.end(), SCustomParamFindName(param));
        if(it != publicParams.end())
        {
            if (((*it).Type() != string("int")) && ((*it).Type() != string("bool")))
            {
                throw std::runtime_error("STelemetryParam::GetIntParam called for not int param");
            }
            int value = boost::lexical_cast<int>((*it).ValueUtf8());
            return value;
        }

        it = std::find_if(readOnlyParams.begin(), readOnlyParams.end(), SCustomParamFindName(param));
        if(it != readOnlyParams.end())
        {
            if ((*it).Type() != string("int"))
            {
                throw std::runtime_error("STelemetryParam::GetIntParam called for not int param");
            }
            int value = boost::lexical_cast<int>((*it).ValueUtf8());
            return value;
        }

        return 0;
    }

private:
    friend class boost::serialization::access;

    template<class TArchive>
    void serialize(TArchive &ar, const unsigned int version)
    {
        ar  & BOOST_SERIALIZATION_NVP(displayId)
            & BOOST_SERIALIZATION_NVP(id)
            & BOOST_SERIALIZATION_NVP(enabled)
            & BOOST_SERIALIZATION_NVP(defaultEnablePatrol)
            & BOOST_SERIALIZATION_NVP(patrolTimeout)
            & BOOST_SERIALIZATION_NVP(savePresetsOnDevice)
            & BOOST_SERIALIZATION_NVP(publicParams)
            & BOOST_SERIALIZATION_NVP(readOnlyParams);

        if (ar.get_library_version() >= 7)
        {
            ar & boost::serialization::make_nvp("metaParams", metaParams);
        }

        serialize_presets(ar);

        ar & boost::serialization::make_nvp("observer", observer);
    }

    template<class TArchive>
    void serialize_presets(TArchive &ar)
    {
        const std::wstring EMPTY_MARK = L"-";
        if(typename TArchive::is_saving())
        {
            TPresets raw(presets);
            BOOST_FOREACH(const TPresets::value_type &i, raw)
                raw[i.first].label = i.second.label.empty() ? EMPTY_MARK : i.second.label;
            ar & boost::serialization::make_nvp("presets", raw);
        }
        else if(typename TArchive::is_loading())
        {
            ar & BOOST_SERIALIZATION_NVP(presets);
            BOOST_FOREACH(TPresets::value_type &i, presets)
            {
                if(EMPTY_MARK == i.second.label)
                    i.second.label.clear();
            }
        }
    }
};
typedef std::vector<STelemetryParam> TTelemetryParams;

struct SRayParam
{
    // The user friendly id.
    unsigned int displayId;

    // The a zero-based number of the ray.
    int contact;

    // The flag indicating whether this ray is enabled.
    int enabled;

    // The ray normal state.
    // The value is zero for "Open circuit" state and non-zero for "Grounded circuit" state.
    int normalState;

    // The flag indicating whether normalState changed.
    bool normalStateChanged;

    SRayParam() : normalStateChanged(false)
    {}

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar  & boost::serialization::make_nvp("displayId", displayId);
        ar  & boost::serialization::make_nvp("contact", contact);
        ar  & boost::serialization::make_nvp("enabled", enabled);
        ar  & boost::serialization::make_nvp("normalState", normalState);
    }

    // Gets normalState value (zero - "Open circuit" state, non-zero - "Grounded circuit" state).
    int get_normalState()  const
    {
        return normalState;
    }

    // Sets normalState value (zero - "Open circuit" state, non-zero - "Grounded circuit" state).
    void set_normalState(int newVal)
    {
        if(normalState != newVal)
        {
            normalState = newVal;
            normalStateChanged = true;
        }
    }

    typedef Mutable EnableType;
    typedef NotIterable IterableType;
};
typedef std::vector<SRayParam> TRayParams;

struct SRelayParam
{
    // The user friendly id.
    unsigned int displayId;

    // The a zero-based relay contact number in the ioDevice.
    int contact;

    // The flag indicating whether this relay is enabled.
    int enabled;

    // The relay normal state.
    // The value is zero for "Open circuit" state and non-zero for "Grounded circuit" state.
    int normalState;

    // The flag indicating whether normalState changed.
    bool normalStateChanged;

    SRelayParam(): normalStateChanged(false)
    {}

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar  & boost::serialization::make_nvp("displayId", displayId);
        ar  & boost::serialization::make_nvp("contact", contact);
        ar  & boost::serialization::make_nvp("enabled", enabled);
        ar  & boost::serialization::make_nvp("normalState", normalState);
    }

    // Gets normalState value (zero - "Open circuit" state, non-zero - "Grounded circuit" state).
    int get_normalState()  const
    {
        return normalState;
    }

    // Sets normalState value (zero - "Open circuit" state, non-zero - "Grounded circuit" state).
    void set_normalState(int newVal)
    {
        if(normalState != newVal)
        {
            normalState = newVal;
            normalStateChanged = true;
        }
    }

    typedef Mutable EnableType;
    typedef NotIterable IterableType;
};
typedef std::vector<SRelayParam> TRelayParams;

struct SIoDeviceParam
{
    // The id of the ioDevice.
    int id;

    // The flag indicating whether the rays and relays are enabled.
    int enabled;

    NStructuredConfig::TCustomParameters publicParams;

    NStructuredConfig::TCustomParameters metaParams;

    // rays and relays config.
    TRayParams   rays;
    TRelayParams relays;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar  & boost::serialization::make_nvp("id", id);
        ar  & boost::serialization::make_nvp("enabled", enabled);
        if (ar.get_library_version() >= 9)
        {
            ar & boost::serialization::make_nvp("publicParams", publicParams);
            ar & boost::serialization::make_nvp("metaParams", metaParams);
        }
        ar  & boost::serialization::make_nvp("rays", rays);
        ar  & boost::serialization::make_nvp("relays", relays);
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SIoDeviceParam> TIoDeviceParams;

typedef NStructuredConfig::TCustomParameters TDetectorParams;
struct SEmbeddedDetectorSettings
{
    std::string     name;
    bool            enabled;
    TDetectorParams privateParams;
    TDetectorParams publicParams;

    // New object parameters for future compactibility.
    NStructuredConfig::TCustomParameters metaParams;

    NStructuredConfig::TVisualElementSettings visual_elements;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int version)
    {
        ar    & boost::serialization::make_nvp("name", name);
        ar    & boost::serialization::make_nvp("enabled", enabled);
        ar    & boost::serialization::make_nvp("private_params", privateParams);
        ar    & boost::serialization::make_nvp("public_params", publicParams);

        if (ar.get_library_version() >= 7)
        {
            ar & boost::serialization::make_nvp("metaParams", metaParams);
        }

        ar    & boost::serialization::make_nvp("visual_elements", visual_elements);
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SEmbeddedDetectorSettings> TEmbeddedDetectorSettings;


enum EStreamType
{
    // 0 - registration stream
    STRegistrationStream = 0,
    // 1 - representation stream.
    STRepresentationStream = 1
};

struct SVideoChannelParam
{
    // The user friendly id.
    unsigned int displayId;

    //To name access point uses format:
    //"hosts/hostname/DeviceIpint.DEVICE_ID/SourceEndpoint.video:CHANNEL_ID:STREAM_TYPE" where:
    // DEVICE_ID - takes from ngp application identifier.
    // CHANNEL_ID - takes from SVideoChannelParam.id
    // STREAM_TYPE - depend on stream function and can take following values:
    //          0 - registration stream
    //          1 - representation stream. Exists if differ from 0 steam.
    // see enum EStreamType


    // The id of the video channel.
    int id;

    // The flag indicating whether the video channel is enabled.
    int enabled;

    // Dynamic video channel params.
    NStructuredConfig::TCustomParameters publicParams;

    // New object parameters for future compactibility.
    NStructuredConfig::TCustomParameters metaParams;

    // Dynamic params for streaming channel.
    TVideoStreamingParams streamings;

    // Collection of embedded detector configurations.
    TEmbeddedDetectorSettings detectors;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int version)
    {
        ar  & boost::serialization::make_nvp("displayId", displayId);
        ar  & boost::serialization::make_nvp("id", id);
        ar  & boost::serialization::make_nvp("enabled", enabled);
        ar  & boost::serialization::make_nvp("publicParams", publicParams);
        
        if (ar.get_library_version() >= 7)
        {
            ar & boost::serialization::make_nvp("metaParams", metaParams);
        }
        
        ar  & boost::serialization::make_nvp("streamings", streamings);
        if (typename TArchive::is_loading())
        {
            // remove consecutive duplicates (by ID)
            streamings.erase(std::unique(streamings.begin(),
                                         streamings.end(),
                                         [](TVideoStreamingParams::value_type const& lhr,
                                            TVideoStreamingParams::value_type const& rhr)
                                         {
                                             return lhr.id == rhr.id;
                                         }),
                             streamings.end());
        }

        ar  & boost::serialization::make_nvp("detectors", detectors);
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SVideoChannelParam> TVideoChannelParams;

struct SRectangleF : ITV8::RectangleF
{
    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar  & boost::serialization::make_nvp("x", left)
            & boost::serialization::make_nvp("y", top)
            & boost::serialization::make_nvp("width", width)
            & boost::serialization::make_nvp("height", height);
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SRectangleF> TRectangleF;

struct SKeyWord
{
    // lighted text
    std::wstring text;

    // The flag indicating whether the text is case sencitive
    int isCaseSensitive;

    // Should light all characters in string with text or not
    int isForFullString;

    int color;

    int colorBackground;

    int colorBackgroundEnabled;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar  & boost::serialization::make_nvp("isCaseSensitive", isCaseSensitive)
            & boost::serialization::make_nvp("isForFullString", isForFullString);
        
        if (ar.get_library_version() >= 10)
            ar & boost::serialization::make_nvp("colorBackgroundEnabled", colorBackgroundEnabled);

        ar  & boost::serialization::make_nvp("text", text)
            & boost::serialization::make_nvp("color", color);

        if (ar.get_library_version() >= 10)
            ar & boost::serialization::make_nvp("colorBackground", colorBackground);
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SKeyWord> TKeyWord;

struct STextFormat
{
    // position ang size of text area
    SRectangleF position;

    // opacity of text area
    double opacity;

    // The font name
    std::string font;

    // The font style
    int fontStyle;

    // The font color
    int color;

    // The font size
    float fontSize;

    // set of ligted words
    TKeyWord keyWords;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar  & boost::serialization::make_nvp("position", position)
            & boost::serialization::make_nvp("opacity", opacity)
            & boost::serialization::make_nvp("font", font)
            & boost::serialization::make_nvp("fontSize", fontSize)
            & boost::serialization::make_nvp("color", color)
            & boost::serialization::make_nvp("fontStyle", fontStyle)
            & boost::serialization::make_nvp("keyWords", keyWords);
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<STextFormat> TTextFormat;

struct SFormatLink
{
    // access point of stream which links to text event source with format
    std::string source;

    // md5 hash of source. Used as end identifier for access_point of end striem
    std::string id;

    // format
    STextFormat format;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int nVersion)
    {
        ar  & boost::serialization::make_nvp("source", source)
            & boost::serialization::make_nvp("id", id)
            & boost::serialization::make_nvp("format", format);
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<SFormatLink> TFormatLink;

struct STextEventSourceParam
{
    // The user friendly id.
    unsigned int displayId;

    // The id of the text event source stream.
    int id;

    // The flag indicating whether the microphone is enabled.
    int enabled;

    // The font name
    std::string font;

    // The font style
    int fontStyle;

    // The font color
    int color;

    // The font size
    float fontSize;

    // The sample duration. Actual for last sample
    int sampleDuration;

    // The sample timestamp offset.
    int sampleOffset = 0;

    // Dynamic text event source params.
    NStructuredConfig::TCustomParameters publicParams;

    // Hided dynamic text event source params.
    NStructuredConfig::TCustomParameters privateParams;

    // New object parameters for future compactibility.
    NStructuredConfig::TCustomParameters metaParams;

    // linked streams with formats
    TFormatLink links;

    // Used only by gui code. Here just for any case
    int eraseAfterEnd;

    // The background color
    int colorBackground;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int version)
    {
        ar  & boost::serialization::make_nvp("displayId", displayId)
            & boost::serialization::make_nvp("id", id)
            & boost::serialization::make_nvp("enabled", enabled)
            & boost::serialization::make_nvp("font", font)
            & boost::serialization::make_nvp("fontSize", fontSize)
            & boost::serialization::make_nvp("color", color)
            & boost::serialization::make_nvp("fontStyle", fontStyle)
            & boost::serialization::make_nvp("sampleDuration", sampleDuration);

        if (ar.get_library_version() >= 13)
            ar & boost::serialization::make_nvp("sampleOffset", sampleOffset);

        ar  & boost::serialization::make_nvp("publicParams", publicParams)
            & boost::serialization::make_nvp("privateParams", privateParams)
            & boost::serialization::make_nvp("links", links);

        if (ar.get_library_version() >= 6)
            ar & boost::serialization::make_nvp("eraseAfterEnd", eraseAfterEnd);
        if (ar.get_library_version() >= 10)
            ar & boost::serialization::make_nvp("colorBackground", colorBackground);
        if (ar.get_library_version() >= 7)
            ar & boost::serialization::make_nvp("metaParams", metaParams);
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};
typedef std::vector<STextEventSourceParam> TTextEventSourceParams;

struct SDeviceNodeParam;
typedef std::vector<SDeviceNodeParam> TDeviceNodeParams;

struct SDeviceNodeParam
{
    // id = index of object. Unique for device for type defined in deviceNodeId.
    unsigned int id;

    // device node element type identificator (id of rep file element)
    std::string deviceNodeId;

    // The flag indicating whether the device node is enabled.
    int enabled;

    // Dynamic params.
    NStructuredConfig::TCustomParameters publicParams;

    // Hided params.
    NStructuredConfig::TCustomParameters privateParams;

    // Additional parameters for future compactibility.
    NStructuredConfig::TCustomParameters metaParams;

    // Child device nodes parametes.
    TDeviceNodeParams children;

    template<typename TArchive>
    void serialize(TArchive& ar, const unsigned int version)
    {
        ar  & boost::serialization::make_nvp("id", id);
        ar  & boost::serialization::make_nvp("deviceNodeId", deviceNodeId);
        ar  & boost::serialization::make_nvp("enabled", enabled);
        ar  & boost::serialization::make_nvp("publicParams", publicParams);
        ar  & boost::serialization::make_nvp("privateParams", privateParams);
        ar  & boost::serialization::make_nvp("metaParams", metaParams);
        ar  & boost::serialization::make_nvp("children", children);
    }

    typedef Mutable EnableType;
    typedef Iterable IterableType;
};

struct SDeviceSettings : SBase
{
    // The user friendly id.
    unsigned int displayId = 0;

    // The uid of event channel which uses for public events (connect, disconnect, etc.) from
    // device.
    std::string             eventChannel;

    // The driver name.
    std::string             driverName;

    // The driver version with format major.minor.revision.
    std::string             driverVersion;

    // The device vendor name.
    std::string             vendor;

    // The model name.
    std::string             model;

    // The models firmware version.
    std::string             firmware;

    // IP address of the device.
    std::string             host;

    // The port of the device.
    int                     port = 0;

    // The MAC address.
    std::string             macAddress;

    // The flag means that to work with device default authentication
    bool                    useDefaultAuthentication = false;

    // The login for device access.
    std::string             login;

    //TODO: Пароль в открытом виде. Шифровать.
    // Хотя в любом случае он идёт через BasicAuth по http, т.е. в открытом виде преобразованный в base64.
    // The password for device access.
    std::string             password;

    // Flag means that we should not set settings to device.
    bool                    blockingConfiguration = true;

    // Dynamic device params.
    NStructuredConfig::TCustomParameters publicParams;

    // New object parameters for future compatibility.
    NStructuredConfig::TCustomParameters metaParams;

    // video channels config.
    TVideoChannelParams videoChannels;

    // microphones config.
    TMicrophoneParams microphones;

    // telemetries config.
    TTelemetryParams telemetries;

    // rays and relays config.
    TIoDeviceParams  ioDevices;

    // audio destination config
    TAudioDestinationParams speakers;

    // text event source config
    TTextEventSourceParams textEventSources;

    // Embedded storages param
    TEmbeddedStoragesParams embeddedStorages;

    // device nodes param
    TDeviceNodeParams   deviceNodes;

    typedef NotMutable EnableType;
    typedef Iterable IterableType;

public:
    // Для совместимости со старым кодом ipint.
    template<class TArchive>
    void Serialize(TArchive &ar)
    {
        serialize(ar, ar.get_library_version());
    }

private:
    friend class boost::serialization::access;

    template<class TArchive>
    void serialize(TArchive &ar, const unsigned int version)
    {
        using namespace boost::serialization;

        ar & make_nvp("displayId", displayId);
        ar & make_nvp("eventChannel", eventChannel);
        ar & make_nvp("driverName", driverName);
        ar & make_nvp("version", driverVersion);
        ar & make_nvp("vendor", vendor);
        ar & make_nvp("model", model);
        ar & make_nvp("firmware", firmware);
        ar & make_nvp("ipAddress", host);
        ar & make_nvp("port", port);
        ar & make_nvp("macAddress", macAddress);
        ar & make_nvp("useDefaultAuthentication", useDefaultAuthentication);

        SBase::serialize_optional(ar, "login", login);
        SBase::serialize_optional(ar, "password", password);

        ar & make_nvp("blockingConfiguration", blockingConfiguration);
        ar & make_nvp("deviceParams", publicParams);
        
        ar & make_nvp("videoChannels", videoChannels);
        ar & make_nvp("microphones", microphones);
        ar & make_nvp("telemetry", telemetries);
        ar & make_nvp("ioDevices", ioDevices);
        ar & make_nvp("speakers", speakers);
        
        if (5 == version)
        {
            ar & make_nvp("textEventSources", textEventSources);
            ar & make_nvp("embeddedStorages", embeddedStorages);
        }
        else if (version >= 6)
        {
            ar & make_nvp("embeddedStorages", embeddedStorages);
            ar & make_nvp("textEventSources", textEventSources);
        }

        if (version >= 7)
        {
            ar & boost::serialization::make_nvp("metaParams", metaParams);
        }

        if (version >= 12)
        {
            ar & boost::serialization::make_nvp("deviceNodes", deviceNodes);
        }
    }
};

template<typename T>
T GetCustomParameter(const std::string& name, const NStructuredConfig::TCustomParameters& params, const T& defaultValue = {})
{
    T value = defaultValue;
    auto it = std::find_if(params.begin(), params.end(), SCustomParamFindName(name));
    if (it != params.end())
    {
        try
        {
            value = boost::lexical_cast<T>(it->ValueUtf8());
        }
        catch (const boost::bad_lexical_cast&)
        {
        }
    }
    return value;
}

}//namespace IPINT30



BOOST_CLASS_IMPLEMENTATION(IPINT30::SVideoStreamingParam, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TVideoStreamingParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SMicrophoneParam, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TMicrophoneParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SDecartPoint, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SPtzPoint, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SCalibrationPoint, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TCalibrationParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::STracker, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TTrackers, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SObserver, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::STelemetryParam, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TTelemetryParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SRayParam, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TRayParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SRelayParam, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TRelayParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SIoDeviceParam, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TIoDeviceParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SEmbeddedDetectorSettings, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TEmbeddedDetectorSettings, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SVideoChannelParam, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TVideoChannelParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SDeviceSettings, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TAudioDestinationParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TEmbeddedStoragesParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SRectangleF, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TRectangleF, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SKeyWord, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TKeyWord, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::STextFormat, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TTextFormat, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SFormatLink, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TFormatLink, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::STextEventSourceParam, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TTextEventSourceParams, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::SDeviceNodeParam, boost::serialization::object_serializable)

BOOST_CLASS_IMPLEMENTATION(IPINT30::TDeviceNodeParams, boost::serialization::object_serializable)

#endif // DEVICEIPINT_3_DEVICESETTINGS_H
