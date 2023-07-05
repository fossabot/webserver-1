#ifndef REGEX_UTILITY_H__
#define REGEX_UTILITY_H__

#include <string>
#include <map>
#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/optional.hpp>

#include "UriCodec.h"

namespace NPluginUtility
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // !!! DEPRECATED !!!
    bool GetQueryStringParameter(const char*& beg, const char*& end,
        const char* paramName, std::string& value);

    // !!! DEPRECATED !!!
    struct SQueryParameter
    {
        const char* const name;
        int value;
    };

    // !!! DEPRECATED !!!
    /// Допускаются только параметры типа SQueryParameter* ////////////////////////////
    bool GetQueryParameters(const char*& beg, const char*& end, int paramCount, ...);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    typedef std::map<std::string /*name*/, std::string /*value*/> TParams;
    typedef std::map<std::wstring /*name*/, std::wstring /*value*/> TParamsW;

    template<class TString, class TParameters>
    bool SeparatePairs(const typename TString::value_type *regExp, const TString &query, TParameters &params)
        /*throw()*/
    {
        using namespace boost;
        typedef typename TString::value_type TChar;
        typedef typename TString::const_iterator TStringItr;

        basic_regex<TChar> NVP(regExp);
        TStringItr begin = query.begin();
        TStringItr end = query.end();

        params.clear();
        try
        {
            match_results<TStringItr> what;
            while(regex_search(begin, end, what, NVP))
            {
                params.insert(std::make_pair(boost::algorithm::to_lower_copy(what[1].str()), NHttpImpl::UriDecode(what[2])));
                begin = what[0].second;
            }

            // query was parsed entirely
            if (begin == end)
            {
                return true;
            }
        }
        catch(const std::exception &) {}

        params.clear();
        return false;
    }

    // Имена параметров приводит к lowercase.
    // Возвращает true, если удалось распарсить запрос.
    // Иначе, в случае ошибки, возвращает false и пустой список параметров, даже если
    // удалось распарсить несколько параметров.

#define TO_WIDE_(literal) L##literal
#define TO_WIDE(literal) TO_WIDE_(literal)

    // The regex here is legacy supporting, the proper one should have the `=` character as mandatory and not optional
    // Something like this: (^\\w+(?:\\.\\w+)*)\\.?=([^=&]+)?(?:&|$)
#define PARSING_REGEX "(^\\w+(?:\\.\\w+)*)\\.?=?((?:[^=&]*)(?:=*))?(?:&|$)"
#define WIDE_PARSING_REGEX TO_WIDE(PARSING_REGEX)

    inline bool ParseParams(const std::string& query, TParams& params)
    /*throw()*/
    {
        return SeparatePairs(PARSING_REGEX, query, params);
    }

    template<class TPs>
    boost::optional<std::string> GetParamOptional(const TPs& params, const typename TPs::key_type& name) noexcept
    {
        const auto& it = params.find(name);
        if (params.end() == it)
        {
            return boost::none;
        }

        return it->second;
    }

    template<class TRes, class TPs>
    void GetParam(const TPs &params, const typename TPs::key_type &name, TRes &value)
        /*throw(std::invalid_argument, boost::bad_lexical_cast)*/
    {
        const typename TPs::const_iterator it = params.find(name);
        if(params.end() == it)
            throw std::invalid_argument("Not found.");

        value = boost::lexical_cast<TRes>(it->second);
    }

    template<class TRes, class TPs>
    TRes GetParam(const TPs &params, const typename TPs::key_type &name)
        /*throw(std::invalid_argument, boost::bad_lexical_cast)*/
    {
        TRes res;
        GetParam(params, name, res);
        return res;
    }

    // В случае ошибки или отсутствия параметра, устанавливает значение по умолчанию.
    template<class TRes, class TPs>
    void GetParam(const TPs &params, const typename TPs::key_type &name, TRes &value, const TRes &defaultValue)
        /*throw()*/
    {
        try
        {
            const typename TPs::const_iterator it = params.find(name);
            if (params.end() != it)
            {
                value = boost::lexical_cast<TRes>(it->second);
                return;
            }
        }
        catch(const std::exception &)
        {
        }
        value = defaultValue;
    }

    template<class TRes, class TPs>
    TRes GetParam(const TPs &params, const typename TPs::key_type &name, const TRes &defaultValue)
        /*throw()*/
    {
        TRes result;
        GetParam(params, name, result, defaultValue);
        return result;
    }

    template<class TRes, class TPs>
    void GetParam(const TPs &params, const typename TPs::key_type &name, const boost::function1<void, const TRes&> &setter)
        /*throw(std::invalid_argument, boost:bad_lexical_cast)*/
    {
        TRes value;
        GetParam(params, name, value);
        if(setter)
            setter(value);
    }

    // В случае ошибки или отсутствия параметра, устанавливает значение по умолчанию.
    template<class TRes, class TPs>
    void GetParam(
        const TPs &params,
        const typename TPs::key_type &name,
        const boost::function1<void, const TRes&> &setter,
        const TRes &defaultValue) /*throw()*/
    {
        TRes result;
        GetParam(params, name, result, defaultValue);
        if (setter) setter(result);
    }

    template<class TPs>
    bool IsParamExists(const TPs &params, const typename TPs::key_type &name)
    {
        return params.end() != params.find(name);
    }
}

#endif // REGEX_UTILITY_H__
