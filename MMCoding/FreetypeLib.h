#ifndef FREETYPE_LIB_H
#define FREETYPE_LIB_H

extern "C"
{
#include <ft2build.h>
#include FT_FREETYPE_H
}

#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>
#include "MMCodingExports.h"

namespace NMMSS
{
    struct IFreetype
    {
        virtual ~IFreetype() {}
        virtual FT_Library Library() = 0;
        virtual boost::mutex::scoped_lock Lock() = 0;
    };
    typedef boost::shared_ptr<IFreetype> PFreetype;

    PFreetype GetFreetype();
}

#endif
