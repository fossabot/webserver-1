#if !defined(ITVSDKUTIL_ISAMPLECONTAINER_H)
#define ITVSDKUTIL_ISAMPLECONTAINER_H

#include <ItvSdk/include/baseTypes.h>
#include "../Sample.h"

namespace ITVSDKUTILES
{
    //Contains the NMMSS::ISample instance.
    struct ISampleContainer : public ITV8::IContract
    {
    public:
        //Detach  NMMSS::ISample instance from container.
        virtual NMMSS::ISample* Detach() = 0;

    protected:
        //Destructor
        virtual ~ISampleContainer()
        {
        }
    };
}

#endif //ITVSDKUTIL_ISAMPLECONTAINER_H
