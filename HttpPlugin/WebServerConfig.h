#ifndef HTTP_PLUGIN_WEB_SERVER_CONFIG_H
#define HTTP_PLUGIN_WEB_SERVER_CONFIG_H

#include <string>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <CorbaHelpers/Envar.h>
#include <Logging/log2.h>

namespace NHttp
{
struct SConfig
{
    std::string Port;
    std::string Prefix;
    int RtspPort;
    int RtspOverHttpPort;
    std::string SslPort;
    std::string CertificateFile;
    std::string PrivateKeyFile;
    bool EnableCORS;
    bool RecodeVideoStream;

    void Parse(std::istream& srcStream, DECLARE_LOGGER_ARG)
    {
        boost::property_tree::ptree propertyTree;
        read_xml(srcStream, propertyTree);

        static const char* const BOOST_KEY = "boost_serialization";
        boost::optional<boost::property_tree::ptree&> boostProperty = propertyTree.get_child_optional(BOOST_KEY);
        if (!boostProperty)
        {
            std::string msg("Can't find " + std::string(BOOST_KEY) + " element");
            throw std::runtime_error(msg);
        }

        LoadConfig(boostProperty.get());

        // Options assigned via environment must be taken with higher priority
        // than the ones read from the configuration file
        checkEnvironmentalOverridings(GET_LOGGER_PTR);
    }
private:
    void LoadConfig(boost::property_tree::ptree const& propertyTree)
    {
        static const char* const HTTP_PORT_KEY = "port";
        static const char* const PREFIX_KEY = "prefix";
        static const char* const RTSP_PORT_KEY = "rtspPort";
        static const char* const RTSP_OVER_HTTP_PORT_KEY = "rtspOverHttpPort";
        static const char* const SSL_PORT_KEY = "sslPort";
        static const char* const CERTIFICATE_KEY = "certificate_file";
        static const char* const PRIVATE_KEY_KEY = "private_key_file";
        static const char* const ENABLE_CORS = "enable_CORS";
        static const char* const RECODE_VIDEO_STREAM = "recode_video_stream";

        Port = propertyTree.get<std::string>(HTTP_PORT_KEY, "80");
        Prefix = propertyTree.get<std::string>(PREFIX_KEY, "/");

        RtspPort = propertyTree.get<int>(RTSP_PORT_KEY, 554);
        RtspOverHttpPort = propertyTree.get<int>(RTSP_OVER_HTTP_PORT_KEY, 8554);

        SslPort = propertyTree.get<std::string>(SSL_PORT_KEY, "443");
        CertificateFile = propertyTree.get<std::string>(CERTIFICATE_KEY, std::string());
        PrivateKeyFile = propertyTree.get<std::string>(PRIVATE_KEY_KEY, std::string());
        EnableCORS = propertyTree.get<bool>(ENABLE_CORS, false);
        RecodeVideoStream = propertyTree.get<bool>(RECODE_VIDEO_STREAM, false);
    }

    void checkEnvironmentalOverridings(DECLARE_LOGGER_ARG)
    {
        auto overrideIntPort = [&](int& option, const std::string& envValue,
                                   const char* const optionName) -> bool
        {
            if (envValue.empty())
                return false;

            auto port = [](const std::string& str)
            {
                try         { return std::stoi(str); }
                catch (...) { return 0; }
            }(envValue);

            if (port > 0)
            {
                _inf_ << optionName << " was overriden by the environment: "
                    << "was: " << option << ", become: " << port;
                option = port;
                return true;
            }
            else
            {
                _wrn_ << "Incorrect value `" << envValue << "` "
                    << "set by the environment for " << optionName << " was ignored";
                return false;
            }
        };

        auto overrideStringPort = [&](std::string& option, const std::string& envValue,
                                      const char* const optionName)
        {
            int tmp;
            if (overrideIntPort(tmp, envValue, optionName))
                option = std::to_string(tmp);
        };

        // Check overridings
        overrideStringPort(Port, NCorbaHelpers::CEnvar::MmssHttpPort(), "HTTP port");
        overrideStringPort(SslPort, NCorbaHelpers::CEnvar::MmssHttpsPort(), "HTTPS port");
        overrideIntPort(RtspPort, NCorbaHelpers::CEnvar::MmssRtspPort(), "RTSP port");
        overrideIntPort(RtspOverHttpPort, NCorbaHelpers::CEnvar::MmssRtspOverHttpPort(), "RTSP/HTTP port");
    }
};
}

#endif
