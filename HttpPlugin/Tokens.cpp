#include <algorithm>
#include <boost/foreach.hpp>
#include <boost/regex.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

#include "Tokens.h"

namespace NPluginUtility
{
    std::string MakeExpression(const PTokens ts)
        /*throw()*/
    {
        const std::string SLASH = "/";
        const std::string OPT_SLASH = "/?";

        std::string res;
        BOOST_FOREACH(const PToken t, *ts)
        {
            if(!res.empty())
                res += t->IsOptional() ? OPT_SLASH : SLASH;
                
            res += t->GetExpression();
        }
        return OPT_SLASH + res + OPT_SLASH;
    }

    void Reset(PTokens ts)
    {
        std::for_each(ts->begin(), ts->end(), boost::bind(&CToken::Reset, _1));
    }

    void Parse(const std::string &src, PTokens dest) 
        /*throw(XFailed)*/
    { 
        using namespace boost;
        const regex exp(MakeExpression(dest));

        Reset(dest);
        try
        {
            smatch what;
            if(regex_match(src.begin(), src.end(), what, exp))
            {
                int i = 1;
                BOOST_FOREACH(PToken t, *dest)
                {
                    const ssub_match &sub = what[i++];
                    if(sub.matched)
                        t->SetValue(sub);
                    else
                        t->SetDefault();
                }
                return;
            }
        }
        catch(const XFailed &) { throw; }
        catch(const std::exception &e) { throw XFailed(e.what()); }

        throw XNotMatch();
    }

    bool ParseSafely(const std::string &src, PTokens dest)
    {
        try
        {
            Parse(src, dest);
            return true;
        }
        catch (const XFailed &) {}
        return false;
    }

    void Parse(const std::string &src, PToken dest)
        /*throw(XFailed)*/
    {
        PTokens path = boost::make_shared<TTokens>();
        path->push_back(dest);
        Parse(src, path);
    }

    void Parse(const PToken src, PTokens dest)
        /*throw(XFailed)*/
    {
        Parse(src->GetValue(), dest);
    }

    void Parse(const PToken src, PToken dest)
        /*throw(XFailed)*/
    {
        Parse(src->GetValue(), dest);
    }

    bool Match(const std::string &src, PTokens dest)
        /*throw()*/
    {
        try
        {
            Parse(src, dest);
            return true;
        }
        catch(const XFailed &) {}
        return false;
    }

    bool Match(const std::string &src, PToken dest)
        /*throw()*/
    {
        PTokens path = boost::make_shared<TTokens>();
        path->push_back(dest);
        return Match(src, path);
    }

    bool Match(const PToken src, PTokens dest)
        /*throw()*/
    {
        const std::string initial = src->GetValue();
        const bool res = Match(initial, dest);
        if(!res)
            src->SetValue(initial);
        return res;
    }

    bool Match(const PToken src, PToken dest)
        /*throw()*/
    {
        return Match(src, boost::make_shared<TTokens>() / dest);
    }

    PToken Token(unsigned int length)
    {
        return PToken(new CToken(length));
    }

    PToken Token(std::string &r, unsigned int l)
    {
        return PToken(new CToken(r, l));
    }

    PMask Mask(const std::string &regExp)
    {
        return PMask(new CMask(regExp));
    }

    PMask Tail()
    {
        return PMask(new CMaskOptional(".*"));
    }

    PMask Empty()
    {
        return Mask("");
    }

    PEndpoint Endpoint()
    {
        return PEndpoint(new CEndpoint());
    }

    PEndpoint Endpoint(std::string &ref)
    {
        return PEndpoint(new CEndpoint(ref));
    }

    PDate Date()
    {
        return PDate(new CDate());
    }

    PDate Date(boost::posix_time::ptime &ref)
    {
        return PDate(new CDate(ref));
    }

    PDate OptionalDate(const boost::posix_time::ptime &defaultValue)
    {
        return PDate(new CDateOptional(defaultValue));
    }

    PDate OptionalDate(boost::posix_time::ptime &ref, const boost::posix_time::ptime &defaultValue)
    {
        return PDate(new CDateOptional(ref, defaultValue));
    }

    PDate Begin()
    {
        return OptionalDate(boost::date_time::max_date_time);
    }

    PDate Begin(boost::posix_time::ptime &ref)
    {
        return OptionalDate(ref, boost::date_time::max_date_time);
    }

    PDate End()
    {
        return OptionalDate(boost::date_time::min_date_time);
    }

    PDate End(boost::posix_time::ptime &ref)
    {
        return OptionalDate(ref, boost::date_time::min_date_time);
    }

    PObjName ObjName(unsigned int length, const std::string &prefix)
    {
        return PObjName(new CObjName(length, prefix));
    }

    PObjName ObjName(NCorbaHelpers::CObjectName &ref, unsigned int length, const std::string &prefix)
    {
        return PObjName(new CObjName(ref, length, prefix));
    }

    PTokens operator/ (PTokens lhs, PToken rhs)
    {
        if(!rhs)
            throw XInvalidToken();
        
        if(!lhs->empty() && lhs->back()->IsOptional() && !rhs->IsOptional())
            throw XInvalidToken();

        lhs->push_back(rhs);
        return lhs;
    }

    PTokens operator/ (PToken lhs, PToken rhs)
    {
        return boost::make_shared<TTokens>() / lhs / rhs;
    }
}