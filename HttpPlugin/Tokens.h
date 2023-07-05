#ifndef TOKENS_H_
#define TOKENS_H_

#include <list>
#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/time_parsers.hpp>
#include <CorbaHelpers/ObjectName.h>

namespace NPluginUtility
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class XFailed : public std::runtime_error
    {
    public:
        explicit XFailed(const std::string &error) : std::runtime_error(error) {}
    };

    class XInvalidToken : public XFailed
    {
    public:
        XInvalidToken() : XFailed("The token is invalid.") {}
    };

    class XNotMatch : public XFailed
    {
    public:
        XNotMatch() : XFailed("Doesn't match.") {}
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CToken;
    typedef boost::shared_ptr<CToken> PToken;
    typedef std::list<PToken> TTokens;
    typedef boost::shared_ptr<TTokens> PTokens;

    class CToken
    {
    public:
        virtual ~CToken() {}

        virtual std::string GetExpression() const
        {
            std::string res;
            for(unsigned int i = 0; i < m_length; ++i)
            {
                if(!res.empty()) res += "/";
                res += "[^/]+";
            }
            return "(" + res + ")";
        }

        virtual void Reset() { m_ref = ""; }

        virtual bool IsOptional() const { return false; }

        virtual void SetDefault() {}

        virtual void SetValue(const std::string &value)
            /*throw(XFailed)*/
        {
            m_ref = value;
        }

        virtual std::string GetValue() const
        {
            return m_ref;
        }

    protected:
        explicit CToken(unsigned int length=1)
            :   m_length(length)
            ,   m_reserve()
            ,   m_ref(m_reserve)
        {
        }

        explicit CToken(std::string &ref, unsigned int length=1)
            :   m_length(length)
            ,   m_reserve()
            ,   m_ref(ref)
        {}

    private:
        friend PToken Token(unsigned int);
        friend PToken Token(std::string &, unsigned int);

    private:
        const unsigned int m_length;
        std::string m_reserve;
        std::string &m_ref;
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CMask;
    typedef boost::shared_ptr<CMask> PMask;

    class CMask : public CToken
    {
    public:
        virtual std::string GetExpression() const
        {
            return "(" + m_mask + ")";
        }

    protected:
        explicit CMask(const std::string &regExp)
            :   m_mask(regExp)
        {}

    private:
        friend PMask Mask(const std::string &);

    private:
        const std::string m_mask;
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CEndpoint;
    typedef boost::shared_ptr<CEndpoint> PEndpoint;

    class CEndpoint : public CToken
    {
    public:
        virtual void SetValue(const std::string &value)
            /*throw(XFailed)*/
        {
            try
            {
                NCorbaHelpers::CObjectName::FromString(value);
            }
            catch(const std::runtime_error &e) { throw XFailed(e.what()); }
            CToken::SetValue(value);
        }

    protected:
        CEndpoint() : CToken(3) {}
        explicit CEndpoint(std::string &ref) : CToken(ref, 3) {}

    private:
        friend PEndpoint Endpoint();
        friend PEndpoint Endpoint(std::string &);
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CObjName;
    typedef boost::shared_ptr<CObjName> PObjName;

    class CObjName : public CToken
    {
    public:
        virtual void SetValue(const std::string &value)
            /*throw(XFailed)*/
        {
            try
            {
                m_ref = NCorbaHelpers::CObjectName::FromString(m_prefix + value);
            }
            catch(const std::exception &e) { throw XFailed(e.what()); }
            CToken::SetValue(value);
        }
        const NCorbaHelpers::CObjectName& Get() const { return m_ref; }

    protected:
        explicit CObjName(unsigned int length, const std::string &prefix="")
            :   CToken(length)
            ,   m_prefix(prefix)
            ,   m_forReferenceOnly()
            ,   m_ref(m_forReferenceOnly)
        {}

        CObjName(NCorbaHelpers::CObjectName &ref, unsigned int length, const std::string &prefix="")
            :   CToken(length)
            ,   m_prefix(prefix)
            ,   m_forReferenceOnly()
            ,   m_ref(ref)
        {}

    private:
        friend PObjName ObjName(unsigned int, const std::string &);
        friend PObjName ObjName(NCorbaHelpers::CObjectName &, unsigned int, const std::string &);

    private:
        const std::string m_prefix;
        NCorbaHelpers::CObjectName m_forReferenceOnly;
        NCorbaHelpers::CObjectName &m_ref;
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CDate;
    typedef boost::shared_ptr<CDate> PDate;

    class CDate : public CToken
    {
    public:
        virtual void Reset()
        {
            m_ref = boost::date_time::not_a_date_time;
            CToken::Reset();
        }

        virtual void SetValue(const std::string &value)
            /*throw(XFailed)*/
        {
            Check(value, m_ref);
            CToken::SetValue(value);
        }

        boost::posix_time::ptime Get() const { return m_ref; }

    protected:
        CDate()
            :   m_reserve()
            ,   m_ref(m_reserve)
        {}

        explicit CDate(boost::posix_time::ptime &ref)
            :   m_reserve()
            ,   m_ref(ref)
        {}

        void Set(const boost::posix_time::ptime &value) { m_ref = value; }

        static void Check(const std::string &src, boost::posix_time::ptime &dest)
            /*throw(XFailed)*/
        {
            const char *PAST = "past";
            const char *FUTURE = "future";

            if(PAST == src)
            {
                dest = boost::date_time::min_date_time;
            }
            else if(FUTURE == src)
            {
                dest = boost::date_time::max_date_time;
            }
            else
            {
                try
                {
                    dest = boost::posix_time::from_iso_string(src);
                }
                catch(const std::exception &e) { throw XFailed(e.what()); }
            }
        }

        friend PDate Date();
        friend PDate Date(boost::posix_time::ptime &);

    private:
        boost::posix_time::ptime m_reserve;
        boost::posix_time::ptime &m_ref;
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CMaskOptional : public CMask
    {
    public:
        virtual bool IsOptional() const { return true; }

        virtual std::string GetExpression() const { return CMask::GetExpression() + "?"; }

    protected:
        explicit CMaskOptional(const std::string &regExp)
            :   CMask(regExp)
        {}

    private:
        friend PMask Tail();
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CDateOptional;
    typedef boost::shared_ptr<CDateOptional> PDateOptional;

    class CDateOptional : public CDate
    {
    public:
        virtual bool IsOptional() const { return true; }

        virtual std::string GetExpression() const { return CToken::GetExpression() + "?"; }

        virtual void SetDefault() { CDate::Set(m_default); }

    protected:
        explicit CDateOptional(const boost::posix_time::ptime &defaultValue)
            :   m_default(defaultValue)
        {}

        CDateOptional(boost::posix_time::ptime &ref, const boost::posix_time::ptime &defaultValue)
            :   CDate(ref)
            ,   m_default(defaultValue)
        {}

        friend PDate OptionalDate(const boost::posix_time::ptime &);
        friend PDate OptionalDate(boost::posix_time::ptime &, const boost::posix_time::ptime &);

    private:
        const boost::posix_time::ptime m_default;
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    PToken    Token(unsigned int length=1);
    PToken    Token(std::string &ref, unsigned int length=1);
    PEndpoint Endpoint();
    PEndpoint Endpoint(std::string &ref);
    PDate     Date();
    PDate     Date(boost::posix_time::ptime &ref);
    PDate     OptionalDate(const boost::posix_time::ptime &defaultValue);
    PDate     OptionalDate(boost::posix_time::ptime &ref, const boost::posix_time::ptime &defaultValue);
    PDate     Begin();
    PDate     Begin(boost::posix_time::ptime &ref);
    PDate     End();
    PDate     End(boost::posix_time::ptime &ref);

    // length - количество частей, из которых состоит имя, без префикса.
    // Например, "/HostName/Service.0" - состоит из двух частей,
    // а "hosts/HostName/Service.0" - из трех.
    PObjName  ObjName(unsigned int length, const std::string &prefix="");
    PObjName  ObjName(NCorbaHelpers::CObjectName &ref, unsigned int length, const std::string &prefix="");

    // Внимание: маска добавляется в группу, поэтому, любые дополнительные подгруппы могут
    // нарушить разбор строки.
    // Например, если маска будет равна "(one)(two)" - то результирующая маска будет - "((one)(two))".
    // Разбор не будет нарушен, если указать маску "(?:one)(?:two)".
    PMask     Mask(const std::string &regExp);
    PMask     Tail();
    PMask     Empty();

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Если маркер (rhs) пустой, то генерирует исключение XInvalidToken.
    // Если в очередь после необязательного маркера, добавляют обязательный,
    // то генерирует исключение XInvalidToken.
    PTokens operator/ (PTokens lhs, PToken rhs);
        /*throw(XInvalidToken)*/

    // Если один из маркеров пустой, то генерирует исключение XInvalidToken.
    // Если в очередь после необязательного маркера, добавляют обязательный,
    // то генерирует исключение XInvalidToken.
    PTokens  operator/ (PToken lhs, PToken rhs);
        /*throw(XInvalidToken)*/

    // Разобрать строку на маркеры.
    // В случае неудачи, генерирует исключение XFailed.
    // Перед выполнением парсинга, очищает содержимое всех маркеров (вызывает Reset()).
    void Parse(const std::string &src, PTokens dest);
        /*throw(XFailed)*/

    // Разобрать строку на маркеры.
    // Не генерирует исключение. В случае ошибки возвращает false
    // Перед выполнением парсинга, очищает содержимое всех маркеров (вызывает Reset()).
    bool ParseSafely(const std::string &src, PTokens dest);

    // см. выше
    void Parse(const std::string &src, PToken dest);
        /*throw(XFailed)*/

    // Использовать в качестве источника другой маркер,
    // для пошагового разбора.
    void Parse(const PToken src, PTokens dest);
        /*throw(XFailed)*/

    // см. выше
    void Parse(const PToken src, PToken dest);
        /*throw(XFailed)*/

    bool Match(const std::string &src, PTokens dest);
        /*throw()*/

    bool Match(const std::string &src, PToken dest);
        /*throw()*/

    bool Match(const PToken src, PTokens dest);
        /*throw()*/

    bool Match(const PToken src, PToken dest);
        /*throw()*/

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}

#endif // TOKENS_H_