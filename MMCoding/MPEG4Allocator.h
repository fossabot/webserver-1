#ifndef __MPEG4__ALLOCATOR__H__
#define __MPEG4__ALLOCATOR__H__

#include <map>
#include "../Codecs/MPEG4/include/mpeg4allocator.h"

class MPEG4AllocatorImpl : public MPEG4_Allocator
{
    static void* GetBufferCallback(uint32_t size, void*  pUserData )
    {
        return ((MPEG4AllocatorImpl*)pUserData)->GetBuffer(size);
    }
    static void LockBufferCallback(void* pBuffer, void* pUserData)
    {
        ((MPEG4AllocatorImpl*)pUserData)->LockBuffer(pBuffer);
    }
    static void UnlockBufferCallback(void* pBuffer, void* pUserData)
    {
        ((MPEG4AllocatorImpl*)pUserData)->UnlockBuffer(pBuffer);
    }
public:
    MPEG4AllocatorImpl()
    {
        pGetBufferCallback    = &MPEG4AllocatorImpl::GetBufferCallback;
        pLockBufferCallback   = &MPEG4AllocatorImpl::LockBufferCallback;
        pUnlockBufferCallback = &MPEG4AllocatorImpl::UnlockBufferCallback;
        pAllocatorData        = this;
    }
private:
    void* GetBuffer(uint32_t size)
    {
        std::map<void*, std::pair<uint32_t, uint32_t> >::iterator ri = m_buffers_map.end();
        uint32_t lower = 0;

        std::map<void*, std::pair<uint32_t, uint32_t> >::iterator mi = m_buffers_map.begin();
        for(; mi != m_buffers_map.end(); ++mi)
        {
            if( (mi->second.second > 0) ||
                (mi->second.first < size) ) continue;

            if( ( (m_buffers_map.end() != ri) && (lower > mi->second.first) ) ||
                  (m_buffers_map.end() == ri) )
            {
                lower = mi->second.first;
                ri = mi;
            }
        }

        if( m_buffers_map.end() == ri )
        {
            void* pBuffer = malloc(size);
            if(!pBuffer) return 0;
            m_buffers_map.insert( std::make_pair(pBuffer, std::make_pair(size,1)) );
            return pBuffer;
        }

        ++(ri->second.second);
        return ri->first;
    }
    void LockBuffer(void* pBuffer)
    {
        std::map<void*, std::pair<uint32_t, uint32_t> >::iterator mi =
        m_buffers_map.find(pBuffer);

        if( mi != m_buffers_map.end() )
            ++(mi->second.second);
    }
    void UnlockBuffer(void* pBuffer)
    {
        std::map<void*, std::pair<uint32_t, uint32_t> >::iterator mi =
        m_buffers_map.find(pBuffer);

        if( ( mi != m_buffers_map.end() ) && (mi->second.second > 0) )
            --(mi->second.second);
    }
    std::map<void*, std::pair<uint32_t,uint32_t> > m_buffers_map;
};

#endif // __MPEG4__ALLOCATOR__H__
