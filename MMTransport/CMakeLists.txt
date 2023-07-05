set(TARGET MMTransport)
project(${TARGET})

ngp_add_library(
    TARGET ${TARGET}
    TYPE SHARED
    DESCRIPTION "Multi-media transport"
    COMMENTS ""
    SOURCES
    ./AbstractRpcClients.cpp
    ./AbstractRpcClients.h
    ./ConnectionAcceptor.cpp
    ./ConnectionAcceptor.h
    ./ConnectionInitiator.cpp
    ./ConnectionInitiator.h
    ./Exports.h
    ./MMTransport.h
    ./QoSPolicyImpl.h
    ./QualityOfService.h
    ./RemoteSource.cpp
    ./SequencePlanner.cpp
    ./SinkEndpoint.cpp
    ./SourceEndpoint.cpp
    ./SourceFactory.cpp
    ./SourceFactory.h
    ./StatisticsCollectorImpl.cpp
    ./StatisticsCollectorImpl.h
)

ngp_use_sdk_module(TARGET ${TARGET} MODULE ACE PRIVATE TAO_TC_IIOP)
ngp_use_sdk_module(TARGET ${TARGET} MODULE JSONCPP PRIVATE jsoncpp)
ngp_use_int_module(${TARGET} MMCoding Network)

target_compile_definitions(
    ${TARGET}
    PRIVATE MMTRANSPORT_EXPORTS
    )

target_include_directories(
    ${TARGET}
    PRIVATE ${NGP_HOME}/mmss
    PRIVATE ${NGP_HOME}/Notification
    )
