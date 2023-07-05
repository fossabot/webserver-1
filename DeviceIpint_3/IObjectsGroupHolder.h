#ifndef DEVICEIPINT3_IOBJECTSGROUPHOLDER_H
#define DEVICEIPINT3_IOBJECTSGROUPHOLDER_H

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/function.hpp>

namespace IPINT30
{

struct IObjectsGroupHolder
{
    virtual ~IObjectsGroupHolder() {}
};

typedef boost::shared_ptr<IObjectsGroupHolder> IObjectsGroupHolderSP;
typedef boost::weak_ptr<IObjectsGroupHolder> IObjectsGroupHolderWP;

struct GroupStateHolder : public IObjectsGroupHolder
{
public:
    typedef boost::function<void()> callback_t;
    explicit GroupStateHolder(callback_t startedCallback, callback_t finishedCallback) : 
        m_finished(finishedCallback)
    {
        if (startedCallback)
        {
            startedCallback();
        }
    }
    ~GroupStateHolder()
    {
        if (m_finished)
        {
            m_finished();
        }
    }
    callback_t m_finished;
};

class OperationCompletion
{
public:
    OperationCompletion() : 
        m_finished(false)
    {
        m_holder = boost::make_shared<GroupStateHolder>(GroupStateHolder::callback_t(),
            boost::bind(&OperationCompletion::handleOperationsFinished, this));
    }

    IObjectsGroupHolderSP getHolder()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        if (!m_holder)
        {
            // Assert.
            throw std::logic_error("Invalid OperationCompletion usage.");
        }
        return IObjectsGroupHolderSP(m_holder);
    }

    void waitComplete()
    {
        IObjectsGroupHolderSP holder;
        {
            boost::mutex::scoped_lock lock(m_holderGuard);
            m_holder.swap(holder);
        }
        holder.reset();

        boost::mutex::scoped_lock lock(m_mutex);
        m_completeCondition.wait(lock,
            boost::bind(&OperationCompletion::m_finished, this) == true);
    }

protected:
    void handleOperationsFinished()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        m_finished = true;
        m_completeCondition.notify_all();
    }

private:
    boost::condition_variable_any m_completeCondition;
    boost::mutex m_mutex;
    bool m_finished;
    boost::mutex m_holderGuard;
    IObjectsGroupHolderSP m_holder;
};

}

#endif

