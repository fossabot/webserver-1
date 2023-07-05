#ifndef DEVICEIPINT3_PULLTOPUSHADAPTER_H
#define DEVICEIPINT3_PULLTOPUSHADAPTER_H

#include <queue>

#include <boost/enable_shared_from_this.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/thread.hpp>
#include <boost/function.hpp>

#include <Logging/log2.h>

#include "IObjectsGroupHolder.h"

namespace IPINT30
{
template<typename TFrameType>
class PushToPullAdapter : 
    public boost::enable_shared_from_this<PushToPullAdapter<TFrameType>>,
    public NLogging::WithLogger
{
public:
    typedef boost::function<bool (TFrameType frame)> frameFilterPredicate_t;
    typedef boost::function<void (TFrameType frame)> callback_t;
    typedef boost::function<void (int internalBufferCount)> stateObserver_t;
    PushToPullAdapter(frameFilterPredicate_t frameFilterPredicate, callback_t callback,
            boost::asio::io_service& service,
            IObjectsGroupHolderWP holder,
            DECLARE_LOGGER_ARG,
            size_t queueMaxFrameLimit = 2000ull) :
        NLogging::WithLogger(GET_LOGGER_PTR),
        m_frameFilterPredicate(frameFilterPredicate),
        m_frameCallback(callback),
        m_strand(service),
        m_queueMaxFrameLimit(queueMaxFrameLimit),
        m_deptFramesCount(0),
        m_weakHolder(holder),
        m_skippedFramesCount(0)
    {
    }

    void receiveFrame(TFrameType frame)
    {
        if (!m_frameFilterPredicate(frame))
        {
            return;
        }

        {
            // TODO: think about calling an error callback.
            boost::mutex::scoped_lock lock(m_framesQueueGuard);
            if (m_queue.size() >= m_queueMaxFrameLimit)
            {
                // The same as overflowThreshold in PlaybackControl
                static constexpr uint32_t SKIPPED_FRAMES_LOG_PERIOD = 1000u;
                const auto firstFrame = m_skippedFramesCount == 0;
                _err_if_(firstFrame || m_skippedFramesCount % SKIPPED_FRAMES_LOG_PERIOD == 0)
                    << "PushToPullAdapter." << this << " Dropping " << (firstFrame ? 1 : m_skippedFramesCount) << " frames due queue overflow";

                ++m_skippedFramesCount;
                return;
            }
            m_queue.push(frame);
        }
        postHandleChanged();
    }

    void request(int count)
    {     
        {
            boost::mutex::scoped_lock lock(m_framesQueueGuard);
            m_deptFramesCount+= count;
        }
        postHandleChanged();
    }
    void atachObserver (stateObserver_t observer)
    {
        boost::mutex::scoped_lock lock(m_callbacksGuard);
        m_observer = observer;
    }

    int detach()
    {
        {
            boost::mutex::scoped_lock lock(m_callbacksGuard);
            m_observer.clear();
            m_frameCallback.clear();
            return m_deptFramesCount;
        }
    }


private:
    void postHandleChanged()
    {
        if (IObjectsGroupHolderSP holder = m_weakHolder.lock())
        {
            m_strand.post(boost::bind(&PushToPullAdapter::handleChanged, 
                this->shared_from_this(), holder));
        }
    }

    void handleChanged(IObjectsGroupHolderSP)
    {
        int currentBufferSize = 0;
        std::vector<TFrameType> readyFrames;
        {
            boost::mutex::scoped_lock lock(m_framesQueueGuard);
            while ((m_deptFramesCount > 0) && !m_queue.empty())
            {
                auto frame = m_queue.front();
                readyFrames.push_back(frame);
                m_queue.pop();
                --m_deptFramesCount;
            }
            currentBufferSize = m_queue.size();
            if (readyFrames.size())
            {
                m_skippedFramesCount = 0;
            }
        }

        callback_t frameCallback;
        stateObserver_t    observer;
        {
            boost::mutex::scoped_lock lock(m_callbacksGuard);
            frameCallback = m_frameCallback;
            observer = m_observer;
        }

        if (observer)
        {
            observer(currentBufferSize);
        }
        
        if (frameCallback)
        {
            std::for_each(readyFrames.begin(), readyFrames.end(), 
                frameCallback);
        }
    }

private:
    frameFilterPredicate_t m_frameFilterPredicate;
    callback_t m_frameCallback;
    typedef std::queue<TFrameType> framesQueue_t;
    framesQueue_t m_queue;
    boost::asio::io_service::strand m_strand;
    const size_t m_queueMaxFrameLimit;
    boost::mutex m_framesQueueGuard;
    int m_deptFramesCount;
    stateObserver_t    m_observer;
    IObjectsGroupHolderWP m_weakHolder;
    boost::mutex m_callbacksGuard;
    uint32_t m_skippedFramesCount;
};

}

#endif

