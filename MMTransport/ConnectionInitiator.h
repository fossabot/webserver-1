#ifndef NGP_MMSS_CONNECTION_INITIATOR_H_
#define NGP_MMSS_CONNECTION_INITIATOR_H_

#include <string>
#include <vector>
#include <boost/function.hpp>
#include <boost/asio.hpp>

#include <mmss/Network/Network.h>

namespace NMMSS
{

// ѕомогает установить соединение. ѕозвол€ет клиентам, которые хот€т соединитьс€ с поставщиком данных,
// изъ€вить свое желаение это сделать. «а них данный объект установит TCP соединение, запишет туда
// строку, по которой его должны опознать, дождетс€ и вычитает приветствие, и если все хорошо, -
// вернет в колбек готовый сокет, на котором можно создать транспортный канал.
class IConnectionInitiator
{
public:
    typedef boost::function1<void, NMMSS::PTCPSocket > FHandler;

    virtual ~IConnectionInitiator(){}

    virtual void InitiateConnection(const std::string& cookie,
        const std::vector<std::string>& addresses, unsigned short port,
        FHandler onConnected) =0;
    virtual void Cancel(const std::string& cookie) =0;
};

typedef boost::shared_ptr<IConnectionInitiator> PConnectionInitiator;
PConnectionInitiator GetConnectionInitiatorInstance();

}

#endif
