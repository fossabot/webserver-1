#include "UrlBuilder.h"

#include <random>
#include <sstream>
#include <vector>
#include <numeric>
#include <algorithm>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/algorithm/string.hpp>

#include <json/json.h>
#include <Crypto/Crypto.h>
#include <SecurityManager/MessageAuth.h>
#include <CorbaHelpers/Uuid.h>

#include "CommonUtility.h"
#include "Constants.h"

namespace
{
    const std::string EXPIRATION_FIELD = "exp";

    std::vector<char> generateSecret()
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int64_t> dis;
        // 64 byte buffer
        static int const KEY_SIZE_BYTES = 64;
        std::vector<char> result(KEY_SIZE_BYTES, 0);
        static_assert(KEY_SIZE_BYTES % sizeof(int64_t) == 0, "Wrong key buffer length");
        auto* bufferBegin = reinterpret_cast<int64_t*>(result.data());
        auto* bufferEnd = reinterpret_cast<int64_t*>(result.data() + result.size());
        std::generate(bufferBegin, bufferEnd, [&]() { return dis(gen); });
        return result;
    }

    std::string base64Hmac(const std::string& payloadToSign, const std::vector<char>& secret)
    {
        std::vector<char> hmac(NSecurityManager::SHA160_DigestLength, 0);
        NSecurityManager::CalculateSHA160HMAC(secret.data(), secret.size(),
            payloadToSign.c_str(), payloadToSign.length(),
            hmac.data());
        return NCrypto::ToBase64Url(hmac.data(), hmac.size());
    }

    const std::string HMAC_PARAM = "&hmac=";
}

TokenAuthenticator::TokenAuthenticator() :
    m_secret(generateSecret())
{   
    m_nonceGenerator = 0;
}

TokenAuthenticator::~TokenAuthenticator()
{

}

std::string TokenAuthenticator::signUrl(const std::string& url)
{
    const int nonce = ++m_nonceGenerator;
    std::ostringstream payloadToSign;
    payloadToSign << url << "&nonce=" << nonce;
    
    payloadToSign << HMAC_PARAM << base64Hmac(payloadToSign.str(), m_secret);

    // returns signed url
    return payloadToSign.str();
}

bool TokenAuthenticator::verify(const std::string& url) const
{
    std::size_t pos = url.find(HMAC_PARAM);
    if (pos == std::string::npos)
        return false;
    const std::string payloadToSign = url.substr(0, pos);
    const std::string providedHmac = url.substr(pos + HMAC_PARAM.length());

    const std::string calculatedHmac = base64Hmac(payloadToSign, m_secret);

    if (providedHmac.length() != calculatedHmac.length())
        return false;
    // Mitigate timing attacks
    std::vector<char> mapped(providedHmac.length(), 0);
    std::transform(providedHmac.begin(), providedHmac.end(), calculatedHmac.begin(), mapped.begin(),
        [](char a, char b) { return a ^ b; });
    return std::accumulate(mapped.begin(), mapped.end(), 0) == 0;
}

UrlBuilder::UrlBuilder(int rtspPort, int rtspOverHttpPort, TokenAuthenticatorSP hmacAuth) :
    m_rtspPort(rtspPort),
    m_rtspOverHttpPort(rtspOverHttpPort),
    m_hmacAuth(hmacAuth)
{
}

namespace
{
std::string makeUrlArchive(const std::string& path,
    const std::string& startTime, int speed, boost::uuids::uuid const& uuid)
{
    //http://IP-адрес:8000/asip-api/archive/media/VIDEOSOURCEID/STARTTIME/20140723T120000.000?speed=1&w=640&h=480&id=f03c6ccf-b181-4844-b09c-9a19e6920fd3
    std::ostringstream stream;
    stream << "archive/" << path << "/" << startTime << "?speed=" << speed 
        << "&id=" << NCorbaHelpers::StringifyUUID(uuid);
    return stream.str();
}
}

std::string signUrl(const std::string& url, boost::posix_time::ptime expiresAt, TokenAuthenticatorSP hmacAuth)
{
    std::ostringstream stream;
    size_t pos = url.find('?');
    char sep = (url.npos == pos) ? '?' : '&';
    stream << url << sep << EXPIRATION_FIELD << "=" << to_iso_string(expiresAt);
    return hmacAuth->signUrl(stream.str());
}

void sendResponseForRtsp(const std::string& u, int rtspPort, int httpPort, bool enableTokenAuth,  
    boost::posix_time::ptime expiresAt, TokenAuthenticatorSP hmacAuth, NHttp::PResponse resp)
{
    std::string directUrl = u;
    std::string proxyUrl = "rtspproxy/" + u;

    if (enableTokenAuth)
    {
        directUrl = signUrl(directUrl, expiresAt, hmacAuth);
        proxyUrl = signUrl(proxyUrl, expiresAt, hmacAuth);
    }

    Json::Value json;
    {
        Json::Value rtsp;
        rtsp["path"] = directUrl;
        rtsp["port"] = std::to_string(rtspPort);
        rtsp["description"] = "RTP/UDP or RTP/RTSP/TCP";
        json["rtsp"] = rtsp;
    }

    {
        Json::Value http;
        http["path"] = proxyUrl;
        http["port"] = std::to_string(httpPort);
        http["description"] = "RTP/RTSP/HTTP/TCP";
        json["http"] = http;
    }

    if (enableTokenAuth)
    {
        Json::Value httpproxy;
        httpproxy["path"] = proxyUrl;
        httpproxy["description"] = "RTP/RTSP/HTTP/TCP Current Http Port";
        json["httpproxy"] = httpproxy;
    }

    NPluginUtility::SendText(resp, Json::FastWriter().write(json));
}

void UrlBuilder::handleLiveRtsp(NHttp::PResponse resp, const std::string& endpoint, bool enableTokenAuth,
    boost::posix_time::ptime expiresAt)
{
    std::string url = endpoint;
    sendResponseForRtsp(url, m_rtspPort, m_rtspOverHttpPort, enableTokenAuth, expiresAt, m_hmacAuth, resp);
}

void UrlBuilder::handleArchiveRtsp(NHttp::PResponse resp, const std::string& endpoint,
    const std::string& startTime, int speed, bool enableTokenAuth, boost::posix_time::ptime expiresAt)
{
    std::string url = makeUrlArchive(endpoint, startTime, speed, NCorbaHelpers::GenerateUUID());
    sendResponseForRtsp(url, m_rtspPort, m_rtspOverHttpPort, enableTokenAuth, expiresAt, m_hmacAuth, resp);
}

void UrlBuilder::handleHttpToken(NHttp::PResponse resp,
    NHttp::PRequest req, TParams params, boost::posix_time::ptime expiresAt)
{
    params.erase(PARAM_ENABLE_TOKEN_AUTH);
    params.erase(PARAM_TOKEN_VALID_HOURS);
    params[EXPIRATION_FIELD] = to_iso_string(expiresAt);

    std::string url = [req, params]
    {
        std::ostringstream stream;
        stream << req->GetPrefix() << req->GetContextPath() << req->GetPathInfo() << "?";
        for (auto p : params)
        {
            stream << p.first << "=" << p.second << "&";
        }
        return boost::algorithm::trim_right_copy_if(stream.str(), boost::is_any_of("?&"));
    }();
    url = m_hmacAuth->signUrl(url);;
    Json::Value json;
    json["path"] = url;

    Json::StreamWriterBuilder writer;
    NPluginUtility::SendText(resp, Json::writeString(writer, json));
}


namespace
{
using namespace NHttp;

class TokenAuthInterceptor: public IInterceptor
{
    DECLARE_LOGGER_HOLDER;

    TokenAuthenticatorSP m_helper;
    PInterceptor m_next;

public:
    TokenAuthInterceptor(DECLARE_LOGGER_ARG, TokenAuthenticatorSP helper, PInterceptor next)
        : m_helper(helper)
        , m_next(next)
    {
        INIT_LOGGER_HOLDER;
    }

    virtual PResponse Process(const PRequest req, PResponse resp)
    {
        try
        {
            do 
            {
                TQueryMap map = req->GetQueryMap();
                const auto cit = map.find(EXPIRATION_FIELD);
                if (cit == map.end())
                    break;
                auto expiresAt = boost::posix_time::from_iso_string(cit->second);
                if (m_helper->verify(req->GetDestination()) && (expiresAt > boost::posix_time::second_clock::local_time()))
                {
                    _inf_ << "Authorized by TokenAuthInterceptor sign";
                    IRequest::AuthSession authSession;
                    authSession.id = TOKEN_AUTH_SESSION_ID;
                    req->SetAuthSession(authSession);
                    return resp;
                }
            } while (false);
        }
        catch (const std::exception& ex)
        {
            _wrn_ << "TokenAuthInterceptor: exception : " << ex.what() << " during parsing url: " << req->GetDestination();
        }

        if (m_next)
        {
            return m_next->Process(req, resp);
        }
        resp->SetStatus(IResponse::Forbidden);
        resp->FlushHeaders();
        _log_ << "TokenAuthInterceptor check is failed.";
        return PResponse();
    }

};

}

NHttp::IInterceptor* CreateTokenAuthInterceptor(DECLARE_LOGGER_ARG, TokenAuthenticatorSP helper, NHttp::PInterceptor next /*= PInterceptor()*/)
{
    return new TokenAuthInterceptor(GET_LOGGER_PTR, helper, next);
}
