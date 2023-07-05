#ifndef ORM_HANDLER_H_
#define ORM_HANDLER_H_

#include "GrpcHelpers.h"

#include <boost/enable_shared_from_this.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <HttpServer/HttpRequest.h>
#include <HttpServer/HttpResponse.h>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class COrmHandler : public boost::enable_shared_from_this<COrmHandler>
{
public:
    struct SParams
    {
        std::string Source;
        boost::posix_time::ptime Begin;
        boost::posix_time::ptime End;
        int Limit;
        int Offset;
        bool ReverseOrder;
        std::vector<std::string> Type;
        std::string Host;
        bool JoinPhases;
        bool LimitToArchive;
        std::string Archive;
        std::string Detector;
    };
    typedef std::shared_ptr<SParams> PParams;

public:
    explicit COrmHandler(DECLARE_LOGGER_ARG) { INIT_LOGGER_HOLDER; }
    virtual ~COrmHandler() {}

    template <typename Derived>
    boost::shared_ptr<Derived> shared_from_base()
    {
        return boost::dynamic_pointer_cast<Derived>(shared_from_this());
    }

    virtual void Process(const NHttp::PRequest, NHttp::PResponse resp, const SParams &params) = 0;

protected:
    DECLARE_LOGGER_HOLDER;
};
typedef boost::shared_ptr<COrmHandler> POrmHandler;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

POrmHandler CreateAlertsHandler(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);
POrmHandler CreateDetectorsHandler(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);
POrmHandler CreateAlertsFullHandler(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);

#endif // ORM_HANDLER_H_
