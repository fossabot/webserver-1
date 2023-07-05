#ifndef DEVICEIPINT3_IPINTDESTROYER_H
#define DEVICEIPINT3_IPINTDESTROYER_H

namespace IPINT30
{

template<class T>
struct ipint_destroyer
{
    void operator()(T *instance) const
    {
        if(0 != instance)
        {
            instance->Destroy();
        }
    }
}; 

}

#endif

