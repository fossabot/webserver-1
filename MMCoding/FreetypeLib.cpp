#include "FreetypeLib.h"
#include <boost/weak_ptr.hpp>

namespace NMMSS
{
    class CFreetype : public IFreetype
    {
        FT_Library   m_library;
        boost::mutex m_mutex;

        public:

        CFreetype()
        {
            FT_Error error = FT_Init_FreeType(&m_library);
            if (error)
                throw std::runtime_error("Error on opening Freetype library: " + std::to_string(error));
        }

        ~CFreetype()
        {
            FT_Done_FreeType(m_library);
        }

        boost::mutex::scoped_lock Lock() override
        {
            return boost::mutex::scoped_lock(m_mutex);
        }

        FT_Library Library() override
        {
            return m_library;
        }
    };

    PFreetype GetFreetype()
    {
        static boost::mutex s_mutex;
        static boost::weak_ptr<IFreetype> s_instance;

        boost::mutex::scoped_lock lock(s_mutex);
        PFreetype instance = s_instance.lock();
        if (!instance)
        {
            instance.reset(new CFreetype());
            s_instance = instance;
        }

        return instance;
    }
}
