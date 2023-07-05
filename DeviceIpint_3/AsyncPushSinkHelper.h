#ifndef MMSS_IPINT_ASYNC_PUSH_SINK_HELPER_H_
#define MMSS_IPINT_ASYNC_PUSH_SINK_HELPER_H_

#include <deque>
#include <stdexcept>
#include <algorithm>
#include <boost/function.hpp>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#include <Logging/log2.h>
#include <CorbaHelpers/Reactor.h>

#include <ItvMediaSdk/include/frameFactorySdk.h>

namespace IPINT30
{

class CAsyncPushSinkHelper : public NLogging::WithLogger
{
    typedef boost::function1<void, ITV8::MFF::IMultimediaBuffer*> FHandler;
public:
    CAsyncPushSinkHelper(DECLARE_LOGGER_ARG, NExecutors::PReactor reactor, FHandler handler)
        : NLogging::WithLogger(GET_LOGGER_PTR)
        , m_reactor(reactor)
        , m_handler(handler)
        , m_isHandling(false)
        , m_isActive(false)
    {
    }
    void Enqueue(ITV8::MFF::IMultimediaBuffer* s)
    {
        boost::mutex::scoped_lock lock(m_mutex);
        m_queue.push_back(s); // TODO: ограничение размера; прореживание.
        // Вообще-то здесь надо было бы использовать CSamplesLimitedQueue.
        // проблема в том, что он оперирует настоящими кадрами.
        // А чтобы построить настоящий кадр из этого добра (ITV8::MFF::IMultimediaBuffer),
        // нужно потратить много CPU-шного времени.
        // поэтому просто ограничиваем очередь фиксированным размером.
        if (m_queue.size() > 64)
        {
            _err_ << "CAsyncPushSinkHelper::Enqueue(): queue size limit exceeded. Draining the queue.";
            Drain(lock);
        }
        CheckHandling(lock);
    }
    void Activate(bool active)
    {
        boost::mutex::scoped_lock lock(m_mutex);
        m_isActive = active;
        CheckHandling(lock); // при необходимости планируем запуск обработчика
    }
    ~CAsyncPushSinkHelper()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        m_isActive = false; // после этого новые обработчики запланированы уже не будут
        while (m_isHandling) // осталось дождаться завершения уже запущенных обработчиков
            m_condition.wait(lock);
        Drain(lock);
    }
private:
    NExecutors::PReactor m_reactor;
    FHandler m_handler;
    boost::mutex m_mutex;
    boost::condition m_condition;
    typedef std::deque<ITV8::MFF::IMultimediaBuffer*> TQueue;
    TQueue m_queue;
    bool m_isHandling; // обработчик запланирован или исполняется
    bool m_isActive; // флаг того, что при поступлении новых данных обработчик будет запланирован
private:
    void Drain(boost::mutex::scoped_lock& lock)
    {
        if (!lock)
            throw std::logic_error("lock should have been acquired");
        for (const auto& it : m_queue)
            it->Destroy();
        m_queue.clear();
    }
    void CheckHandling(boost::mutex::scoped_lock& lock)
    {
        if(!lock)
            throw std::logic_error("lock should have been acquired");
        if (m_isActive && !m_isHandling && !m_queue.empty())
        {
            m_isHandling = true;
            PostHandle(lock);
        }
    }
    void StopHandling(boost::mutex::scoped_lock& lock)
    {
        if (!lock)
            throw std::logic_error("lock should have been acquired");
        m_isHandling = false;
        m_condition.notify_all();
    }
    void PostHandle(boost::mutex::scoped_lock& lock)
    {
        if (!lock)
            throw std::logic_error("lock should have been acquired");
        if (!m_isHandling)
            throw std::logic_error("m_isHandling should be set");

        m_reactor->GetIO().post(boost::bind(&CAsyncPushSinkHelper::Handle, this));
    }
    void Handle()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        if (!m_queue.empty() && m_isActive)
        {
            ITV8::MFF::IMultimediaBuffer* s = m_queue.front();
            m_queue.pop_front();
            lock.unlock();
            try
            {
                m_handler(s);
            }
            catch (...)
            {
                _wrn_ << "CAsyncPushSinkHelper::Handle(): frame handler has thrown an unhandled exception";
            }
            lock.lock();
        }
        if (!m_queue.empty() && m_isActive)
            PostHandle(lock);
        else
            StopHandling(lock);
    }
};

}

#endif
