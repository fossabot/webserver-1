#ifndef RTSP_URL_BUILDER_H
#define RTSP_URL_BUILDER_H

#include <cstdint>
#include <string>
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/date_time/posix_time/ptime.hpp>
#include <atomic>

#include <HttpServer/HttpServer.h>

class TokenAuthenticator
{
public:
    TokenAuthenticator();
    ~TokenAuthenticator();

    std::string signUrl(const std::string& url);
    bool verify(const std::string& url) const;

private:
    const std::vector<char> m_secret;
    std::atomic_int m_nonceGenerator;
};


typedef std::shared_ptr<TokenAuthenticator> TokenAuthenticatorSP;

class UrlBuilder
{
public:
    UrlBuilder(int rtspPort, int rtspOverHttpPort, TokenAuthenticatorSP hmacAuth);

    void handleLiveRtsp(NHttp::PResponse resp, const std::string& endpint,
        bool enableTokenAuth, boost::posix_time::ptime expiresAt);

    void handleArchiveRtsp(NHttp::PResponse resp,
        const std::string& endpint, const std::string& startTime,
        int speed, bool enableTokenAuth, boost::posix_time::ptime expiresAt);
    
    typedef std::map<std::string /*name*/, std::string /*value*/> TParams;

    void handleHttpToken(NHttp::PResponse resp, NHttp::PRequest req,
        TParams params, boost::posix_time::ptime expiresAt);

private:
    const int m_rtspPort;
    const int m_rtspOverHttpPort;
    TokenAuthenticatorSP m_hmacAuth;
};

typedef std::shared_ptr<UrlBuilder> UrlBuilderSP;

NHttp::IInterceptor* CreateTokenAuthInterceptor(DECLARE_LOGGER_ARG
    , TokenAuthenticatorSP
    , NHttp::PInterceptor next = NHttp::PInterceptor());

#endif
