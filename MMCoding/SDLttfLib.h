#ifndef SDLTTF_LIB_H
#define SDLTTF_LIB_H

#include "../mIntTypes.h"

extern "C"
{
#include "SDL_ttf.h"
}

#include <boost/thread/mutex.hpp>

#include "MMCodingExports.h"

namespace NMMSS
{
    class MMCODING_CLASS_DECLSPEC CSDLttfLib
    {
    public:
        static boost::mutex::scoped_lock Lock();
        ~CSDLttfLib();

    private:
        CSDLttfLib();
        static CSDLttfLib& Instance();

        class Impl;

    private:
        Impl* const m_impl;
    };
}


#endif
