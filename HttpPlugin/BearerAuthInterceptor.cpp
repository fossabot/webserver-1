#include "HttpPlugin.h"
#include "Constants.h"

#include <json/json.h>

#include <CorbaHelpers/Uuid.h>
#include <SecurityManager/BasicTypes.h>

namespace
{
    using namespace NHttp;

    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    static inline bool is_base64(unsigned char c) {
        return (isalnum(c) || (c == '+') || (c == '/'));
    }

    std::string base64_decode(std::string const& encoded_string) {
        int in_len = encoded_string.size();
        int i = 0;
        int j = 0;
        int in_ = 0;
        unsigned char char_array_4[4], char_array_3[3];
        std::string ret;

        while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
            char_array_4[i++] = encoded_string[in_]; in_++;
            if (i == 4) {
                for (i = 0; i < 4; i++)
                    char_array_4[i] = base64_chars.find(char_array_4[i]);

                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

                for (i = 0; (i < 3); i++)
                    ret += char_array_3[i];
                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 4; j++)
                char_array_4[j] = 0;

            for (j = 0; j < 4; j++)
                char_array_4[j] = base64_chars.find(char_array_4[j]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
        }

        return ret;
    }

    class CBearerAuth : public IInterceptor
    {
        DECLARE_LOGGER_HOLDER;

        NCorbaHelpers::WPContainer m_container;
        PInterceptor m_next;

    public:
        CBearerAuth(DECLARE_LOGGER_ARG
            , PInterceptor next)
            : m_next(next)
        {
            INIT_LOGGER_HOLDER;
        }

        virtual PResponse Process(const PRequest req, PResponse resp)
        {
            std::string ctx = req->GetContextPath();

            try
            {
                THttpHeaders h = req->GetHeaders();
                boost::optional<std::string> auth = GetHeader<std::string>(h, "Authorization");

                std::string method, authdata;

                if (auth)
                {
                    std::istringstream iss(*auth);
                    iss >> method >> authdata;
                }
                else
                {
                    const auto& params = req->GetQueryMap();
                    auto it = params.find("auth_token");
                    if (params.end() != it)
                    {
                        authdata = it->second;
                        method = "Bearer";
                    }
                }

                if (method == "Bearer")
                {
                    NHttp::IRequest::PChangedAuthSessionData session(std::make_shared<std::string>(authdata), NHttp::IRequest::PAuthSessionData());

                    std::size_t pos1 = authdata.find('.');
                    std::size_t pos2 = authdata.find('.', ++pos1);
                    if (std::string::npos != pos1 && std::string::npos != pos2)
                    {
                        std::string userInfo(authdata.substr(pos1, pos2 - pos1));
                        std::string ui(base64_decode(userInfo));

                        _log_ << "Auth token decoded: " << ui;

                        Json::Value root;
                        Json::Reader reader;
                        bool status = reader.parse(ui.c_str(), root);
                        if (status)
                        {
                            auto setAuthSession = [&](std::string authUser)
                            {
                                NHttp::IRequest::AuthSession auth;
                                auth.id = LOGIN_PASSWORD_AUTH_SESSION_ID;
                                auth.data = session;
                                auth.user = std::move(authUser);

                                req->SetAuthSession(auth);
                            };

                            if (root.isMember("lgn"))
                            {
                                setAuthSession(root["lgn"].asString());
                                return resp;
                            }
                            else if (NCorbaHelpers::CEnvar::IsArpagentBridge())
                            {
                                static const auto systemRoleId = NCorbaHelpers::StringifyUUID(NSecurityManager::GetSystemRoleId());
                                const auto& role = root["rls"];

                                // In the bridge mode, there must be only one role in the Token - "System Role".
                                if (!role.isNull() && role[0].asString() == systemRoleId)
                                {
                                    setAuthSession(std::string {});
                                    return resp;
                                }
                            }
                            
                            _wrn_ << "JWT payload does not contain information about user or system role";
                        }
                        else
                            _wrn_ << "JWT payload parsing error";                              
                    }
                }
            }
            catch (const std::exception& e)
            {
                _err_ << "Bearer authentication error: " << e.what();
            }

            if (m_next)
            {
                return m_next->Process(req, resp);
            }

            resp->SetStatus(IResponse::Forbidden);
            resp->FlushHeaders();
            return PResponse();
        }
    };
}

namespace NHttp
{
    IInterceptor* CreateBearerAuthInterceptor(
        DECLARE_LOGGER_ARG, PInterceptor next)
    {
        return new CBearerAuth(GET_LOGGER_PTR, /*c,*/ next);
    }
}
