#include "SDLttfLib.h"

#include <mutex>

namespace NMMSS
{
    namespace
    {
        static std::unique_ptr<NMMSS::CSDLttfLib> theInstance;
        std::once_flag theFlag;
    }


    class CSDLttfLib::Impl
    {
    public:
        boost::mutex m_mutex;
    };

    boost::mutex::scoped_lock CSDLttfLib::Lock()
    {
        return boost::mutex::scoped_lock(Instance().m_impl->m_mutex);
    }

    CSDLttfLib::CSDLttfLib() :
        m_impl(new Impl)
    {
        TTF_Init();
    }

    CSDLttfLib::~CSDLttfLib()
    {
        TTF_Quit();
        delete m_impl;
    }


    CSDLttfLib& CSDLttfLib::Instance()
    {
        std::call_once(theFlag, []()
        { 
            theInstance.reset(new CSDLttfLib);
        });

        return *theInstance;
    }

}


