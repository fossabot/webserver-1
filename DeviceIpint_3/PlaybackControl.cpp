#include "PlaybackControl.h"

#include <boost/function.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>

#include <CorbaHelpers/GccUtils.h>

GCC_SUPPRESS_WARNING_BEGIN((-Wunused-local-typedefs));
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/euml/common.hpp>
#include <boost/msm/front/euml/operator.hpp>
GCC_SUPPRESS_WARNING_END()

#include "PlaybackControl.inl"
#include <Logging/log2.h>

namespace IPINT30
{

PlaybackControl::PlaybackControl(boost::asio::io_service& service, 
                                 callback_t errorCallback,
                                 int underflowThreshold,
                                 int overflowThreshold) : 
    m_service(service),
    m_errorCallback(errorCallback),
    m_underflowThreshold(underflowThreshold),
    m_overflowThreshold(overflowThreshold)
{
}

namespace
{

// Flags
struct FlagWaitingForHandler;

// States definition

// Controlled IPINT object executes command in this state,
struct StateCommandSent : public boost::msm::front::state<>
{
    typedef boost::mpl::vector1<FlagWaitingForHandler> flag_list;
};

struct StateIdle : public boost::msm::front::state<> {};
struct StateSeekSent : public StateCommandSent {};
struct StatePlaySent : public StateCommandSent {};
struct StatePauseSent : public StateCommandSent {};
struct StatePlaying : public boost::msm::front::state<> {};
struct StatePaused : public boost::msm::front::state<> {};

// Events

struct OperationIsCompleated {};

struct EventDoSeek
{
    EventDoSeek(ITV8::timestamp_t timestamp) : 
        m_timestamp(timestamp)
    {}

    ITV8::timestamp_t    m_timestamp;
};
struct EventOperationOk {};
struct EventQueueOverflow {};
struct EventQueueUnderflow {};
struct EventError
{
    EventError(const ITV8::hresult_t code) :
        m_error(code)
    {}
    
    ITV8::hresult_t m_error;
};

struct EventUserCancel {};

// Modes
struct ModeNormal : public boost::msm::front::state<> {};
// In this state state machine will ignore all events other than in OperationIsCompleated
struct ModeUserCancelled : public boost::msm::front::interrupt_state<OperationIsCompleated> {};
struct ModeTerminating: public boost::msm::front::interrupt_state<OperationIsCompleated>
{
    template <class TEvent, class Fsm> void on_entry(const TEvent& event, 
        Fsm& stateMachine)
    {
        stateMachine.sendTeardown();
    }
};

struct ModeTerminated : public boost::msm::front::terminate_state<>
{
    template <class TEvent, class Fsm> void on_entry(const TEvent& event, 
        Fsm& stateMachine)
    {
        stateMachine.release();
    }
};
// Guards
struct IsBusy 
{ 
    template <class Fsm,class Evt,class SourceState,class TargetState> 
    bool operator()(Evt const&, Fsm& fsm, SourceState&, TargetState& ) 
    {
        return fsm.template is_flag_active<FlagWaitingForHandler>();
    } 
}; 

typedef boost::shared_ptr<detail::PlaybackStateMachine> PlaybackStateMachineSP;

using namespace boost::msm::front;
using namespace boost::msm::front::euml;

// The state machine definition class.
// When user wants explicitly terminate current state machine
// things get a bit complicated.
// If we sent any command (such as play, pause commands) then we need to wait 
// while IPINT object completes executing current operation (calls operation handler)
// before we terminate state machine.
// To handle this, following machinery is used:
// - When we need to wait for callback before terminating state machine
// state machine translates in "ModeUserCancelled" state
// - ModeUserCancelled is class of *interrupt_state*
// in this state state machine will ignore all events except OperationIsCompleated
// - After operation handler called by user, OperationIsCompleated  event is generated
// so state machine translates to "ModeTerminated" state.
// - After state machine translates to ModeTerminated state, 
// it gives app processing all events and releases internal reference, so it will be deleted 
// right after all posted handlers is executed

struct PlayerStateMachineDef : public state_machine_def<PlayerStateMachineDef>
{
    typedef boost::mpl::vector<StateIdle, ModeNormal> initial_state;

    void sendSeek(const EventDoSeek& seekEvent);

    template<typename TEvent>
    void sendPlay(const TEvent&);
    
    void sendPause(const EventQueueOverflow&);
    
    void release();

    void reportError(const EventError& event);

    void sendTeardown();

    typedef PlayerStateMachineDef p;
    struct transition_table : boost::mpl::vector<
        //            Start            Event                Next                Action           Guard
        //    +------------------+---------------------+-------------------+-----------------+-------------+
        
        //------------------------ Error/normal operation states region ---------------------
        a_row<ModeNormal,        EventError,                ModeTerminating,    &p::reportError                >,
        Row<ModeNormal,            EventUserCancel,        ModeTerminating,    none,            Not_<IsBusy>>,
        Row<ModeNormal,            EventUserCancel,        ModeUserCancelled,    none,            IsBusy        >,
        _row<ModeUserCancelled,    OperationIsCompleated,    ModeTerminating                                    >,
        _row<ModeTerminating,    OperationIsCompleated,    ModeTerminated                                    >,
        
        //-----------------------Main operations state region ------------------------------
        a_row<StateIdle,        EventDoSeek,            StateSeekSent,        &p::sendSeek            >,    
        a_row<StateSeekSent,    EventOperationOk,        StatePlaySent,        &p::sendPlay            >,
        _row<StatePlaySent,        EventOperationOk,        StatePlaying                                >,
        a_row<StatePlaying,        EventQueueOverflow,        StatePauseSent,        &p::sendPause            >,
        _row<StatePauseSent,    EventOperationOk,        StatePaused                                    >,
        a_row<StatePaused,        EventQueueUnderflow,    StatePlaySent,        &p::sendPlay            >
    > {};

    template <class FSM,class Event>
    void no_transition(const Event& event, FSM&, int state)
    {
        // Do nothing. It is expected that there were be events
        // with no transition for current state
    }

    PlaybackControl::callback_t getOperationHandler();

private:
    void handleOperation(ITV8::hresult_t error);
    void handleTeradown(ITV8::hresult_t error);


    virtual PlaybackStateMachineSP getMachine();


protected:
    boost::shared_ptr<PlaybackControl::IWrappedPlayback> m_wrappedPlayback;
    PlaybackStateMachineSP m_stateMachine;
};

void PlayerStateMachineDef::sendSeek(const EventDoSeek& seekEvent)
{
    m_wrappedPlayback->seek(seekEvent.m_timestamp,
        getOperationHandler());
}

void PlayerStateMachineDef::sendPause(const EventQueueOverflow&)
{
    m_wrappedPlayback->pause(
        getOperationHandler());
}

template<typename TEvent>
void PlayerStateMachineDef::sendPlay(const TEvent& seekEvent)
{
    m_wrappedPlayback->play(getOperationHandler());
}

void PlayerStateMachineDef::sendTeardown()    
{
    m_wrappedPlayback->teardown(getOperationHandler());
}


} // end of anonimus namespace

typedef boost::msm::back::state_machine<PlayerStateMachineDef> PlayerStateMachineBase_t;

class detail::PlaybackStateMachine : public PlayerStateMachineBase_t,
    public boost::enable_shared_from_this<detail::PlaybackStateMachine>
{
public:
    typedef boost::function<void(ITV8::hresult_t)> errorCallback_t;
    PlaybackStateMachine(PlaybackControl::IWrappedPlaybackSP playback,
            IObjectsGroupHolderSP stateHolder,
            errorCallback_t callback,
            boost::asio::io_service& service,
            const std::string& readerName,
            DECLARE_LOGGER_ARG): 
        m_stateHolder(stateHolder),
        m_callback(callback),
        m_strand(service),
        m_readerName(readerName)
    {
        INIT_LOGGER_HOLDER;
        PlayerStateMachineBase_t::m_wrappedPlayback = playback;
        _inf_ << "Playback state machine created = " << this << ", readerName = " << m_readerName;
    }

    ~PlaybackStateMachine()
    {
        _inf_ << "Playback state machine destroyed = " << this << ", readerName = " << m_readerName;
    }

    void reportError(ITV8::hresult_t error)
    {
        errorCallback_t calllback;
        {
            boost::mutex::scoped_lock lock(m_callBackguard);
            calllback = m_callback;
        }
        if (calllback)
        {
            calllback(error);
        }
    }

    void init(ITV8::timestamp_t timestamp)
    {
        PlayerStateMachineBase_t::m_stateMachine = shared_from_this();
        start();
        process_event(EventDoSeek(timestamp));
    }
    
    // All State machine actions executed in m_strand handlers,
    // to machine in thread safe way. 
    // State machine will be destroyed, after all shared 
    // references is released. To prevent destroying 
    // state machine in current handler, call this method
    void deferObjectDestroing()
    {
        m_strand.post(boost::bind(&PlaybackStateMachine::defferedDestroy,
            shared_from_this()));
    }
    
    boost::asio::io_service::strand& strand() { return m_strand; }

    void detach()
    {
        _inf_ << "Playback state machine detached = " << this << ", readerName = " << m_readerName;
        boost::mutex::scoped_lock lock(m_callBackguard);
        m_callback.clear();
    }

private:
    static void defferedDestroy(PlaybackStateMachineSP)    {}

private:
    IObjectsGroupHolderSP    m_stateHolder;
    errorCallback_t            m_callback;
    boost::asio::io_service::strand        m_strand;
    const std::string m_readerName;
    boost::mutex            m_callBackguard;
    DECLARE_LOGGER_HOLDER;
};

template<typename TEvent>
void IPINT30::PlaybackControl::postEvent()
{
    StateMachineSP machine = getStateMachine();
    if (machine)
    {
        machine->strand().post(boost::bind(&detail::PlaybackStateMachine::process_event<TEvent>,
            machine, TEvent()));
    }
}

void PlaybackControl::terminate()
{
    postEvent<EventUserCancel>();
    if (StateMachineSP machine = getStateMachine())
    {
        machine->detach();
    }
    {
        boost::mutex::scoped_lock lock(m_stateMachineGuard);
        m_stateMachine.reset();
    }
}

PlaybackControl::StateMachineSP PlaybackControl::getStateMachine()
{
    StateMachineWP result;
    {
        boost::mutex::scoped_lock lock(m_stateMachineGuard);
        result = m_stateMachine;
    }
    return result.lock();
}

PlaybackControl::StateMachineSP PlaybackControl::createStateMachine(IObjectsGroupHolderSP stateHolder,
    IWrappedPlaybackSP playback, const std::string& readerName, DECLARE_LOGGER_ARG)
{
    // Create state machine
    StateMachineSP machine = boost::make_shared<detail::PlaybackStateMachine>(
        playback, stateHolder, m_errorCallback, boost::ref(m_service), readerName, GET_LOGGER_PTR);
    return machine;
}

namespace
{
// Light copyable functor for wrapping inside boost::function
struct WeakObserver
{
    typedef boost::weak_ptr<detail::PlaybackStateMachine> StateMachineWP;
    typedef boost::shared_ptr<detail::PlaybackStateMachine> StateMachineSP;

    WeakObserver(StateMachineSP machine, int underflowThreshold, int overflowThreshold) :
        m_machineWearRef(machine),
        m_underflowThreshold(underflowThreshold),
        m_overflowThreshold(overflowThreshold)
    {}

    void operator()(int count)
    {

        if (count > m_overflowThreshold)
        {
            postEvent<EventQueueOverflow>();
        }
        else if (count < m_underflowThreshold)
        {
            postEvent<EventQueueUnderflow>();
        }
    }
private:
    template<typename TEvent>
    void postEvent()
    {
        if (StateMachineSP machine = m_machineWearRef.lock())
        {
            machine->strand().post(boost::bind(&detail::PlaybackStateMachine::process_event<TEvent>,
                machine, TEvent()));
        }
    }
    
private:
    StateMachineWP m_machineWearRef;
    const int m_underflowThreshold;
    const int m_overflowThreshold;
};
}

boost::function<void(int)> PlaybackControl::getQueueObserver(StateMachineSP machine)
{
    return WeakObserver(machine, m_underflowThreshold, m_overflowThreshold);
}

void PlaybackControl::registerAndStart(StateMachineSP machine, ITV8::timestamp_t timestamp)
{
    // Post init task
    {
        boost::mutex::scoped_lock lock(m_stateMachineGuard);
        m_stateMachine = machine;
    }
    machine->strand().post(
        boost::bind(&detail::PlaybackStateMachine::init, machine, timestamp));    
}



namespace
{
void PlayerStateMachineDef::reportError(const EventError& event)
{
    getMachine()->reportError(event.m_error);
}

PlaybackStateMachineSP PlayerStateMachineDef::getMachine()
{
    return m_stateMachine;
}

void PlayerStateMachineDef::handleOperation(ITV8::hresult_t error)
{
    PlaybackStateMachineSP machine = getMachine();
    machine->process_event(OperationIsCompleated());
    if (error)
    {
        machine->process_event(EventError(error));
    }
    else
    {
        machine->process_event(EventOperationOk());
    }
}

void PlayerStateMachineDef::release()
{
    PlaybackStateMachineSP tmp;
    m_stateMachine.swap(tmp);
    // This method is called from state machine event processing loop
    // so we delegate objects destroying to next strand handler.
    tmp->deferObjectDestroing();
}

PlaybackControl::callback_t PlayerStateMachineDef::getOperationHandler()
{    
    return m_stateMachine->strand().wrap(
        bind(&PlayerStateMachineDef::handleOperation, this, _1));
}

} // end of anonymous namespace


}
