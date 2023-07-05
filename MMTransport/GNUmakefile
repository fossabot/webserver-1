TARGET_OBJ  = MMTransport

INCLUDE_PATH = Primitives Notification Notification/Notification_IDL \
               mmss mmss/MMIDL

DEFINITIONS  = MMTRANSPORT_EXPORTS

DEPEND_DIRS = mmss                          \
              mmss/Network                  \
              mmss/MMIDL                    \
              mmss/MMCoding                 \
              Primitives/Logging            \
              Primitives/CorbaHelpers       \
              CIS/InfraServer_IDL	        \
              Primitives/ChaosMonkey        \
              Primitives/Executors          \
              Primitives/Utils

INT_LIBS = MMSS         \
           Network      \
           MMIDL        \
           MMCoding     \
           CorbaHelpers \
           OVCodec      \
           Logging      \
           InfraServer_IDL \
           ChaosMonkey  \
           Executors    \
           Utils

EXT_LIBS = $(TAO_COMMON_LIBS) TAO_CosNaming TAO_TC TAO_TC_IIOP jsoncpp
BOOST_LIBS = $(BOOST_COMMON_LIBS) $(BOOST_SERIALIZATION_LIBS) regex

OBJECTS = SinkEndpoint \
          SourceEndpoint \
          SourceFactory \
          SequencePlanner \
          AbstractRpcClients \
          ConnectionAcceptor \
          ConnectionInitiator \
          StatisticsCollectorImpl \
          RemoteSource

CXXFLAGS = -Werror

include ../../Makefile.common
