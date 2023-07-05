#ifndef DEVICEIPINT3_SDKHELPERS_H
#define DEVICEIPINT3_SDKHELPERS_H
#include <string>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/any.hpp>
#include <CorbaHelpers/Unicode.h>


//TODO: Перенести в ItvSdkUtil

namespace ITV8
{
//Contains pair: error cod and it text description. 
struct SErrorMessageItem
{
    hresult_t err;
    const char* msg;
};

//Allows to get text description from ITV8::TErrorCode field name.
#define ITV8_ERROR_MESSAGE_ITEM(error_hresult__) {error_hresult__, #error_hresult__}

//TODO:Перенести тело метода в cpp.
//Gets the message from ITV8 error code.
inline std::string get_last_error_message(hresult_t err)
{
    static std::string NO_MESSAGE("");
    static SErrorMessageItem map[] = 
    {
        ITV8_ERROR_MESSAGE_ITEM(EGeneralError),
        ITV8_ERROR_MESSAGE_ITEM(EUnknownError),
        ITV8_ERROR_MESSAGE_ITEM(EAlready),
        ITV8_ERROR_MESSAGE_ITEM(EInternalError),
        ITV8_ERROR_MESSAGE_ITEM(ENoSuchProperty),
        ITV8_ERROR_MESSAGE_ITEM(EValueOutOfRange),
        ITV8_ERROR_MESSAGE_ITEM(EInvalidPropertyType),
        ITV8_ERROR_MESSAGE_ITEM(EUnsupportedCommand),

        // Connection errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralConnectionError),
        // translated boost::asio errors
        ITV8_ERROR_MESSAGE_ITEM(EAccessDenied),
        ITV8_ERROR_MESSAGE_ITEM(EAddressFamilyNotSupport),
        ITV8_ERROR_MESSAGE_ITEM(EAddressInUse),
        ITV8_ERROR_MESSAGE_ITEM(EConnectionRefused),
        ITV8_ERROR_MESSAGE_ITEM(EConnectionReset),
        ITV8_ERROR_MESSAGE_ITEM(EHostUnreachable),
        ITV8_ERROR_MESSAGE_ITEM(ENetworkDown),
        ITV8_ERROR_MESSAGE_ITEM(EEOF),

        ITV8_ERROR_MESSAGE_ITEM(EAuthorizationFailed),
        ITV8_ERROR_MESSAGE_ITEM(EUrlNotFound),

        // Device errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralDeviceError),
        ITV8_ERROR_MESSAGE_ITEM(EDeviceReboot),

        // Autodetect errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralAutodetectError),
        ITV8_ERROR_MESSAGE_ITEM(EWrongDeviceDetected),
        ITV8_ERROR_MESSAGE_ITEM(EBrandNotSupported),

        // Search errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralSearchError),

        // VideoSource errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralVideoSourceError),

        // AudioSource errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralAudioSourceError),

        // AudioDestination errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralAudioDestinationError),

        // IODevice errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralIODeviceError),

        // Telemetry errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralTelemetryError),

        // SerialPort errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralSerialPortError),

        // Parameters errors
        ITV8_ERROR_MESSAGE_ITEM(EGeneralParametersError),
        ITV8_ERROR_MESSAGE_ITEM(EBlockingConfiguration),
        ITV8_ERROR_MESSAGE_ITEM(EParameterConflict),

        ITV8_ERROR_MESSAGE_ITEM(EFirstUserError)

    };

    for(size_t i=0;i<sizeof(map)/sizeof(SErrorMessageItem);i++)
    {
        if(map[i].err == err)
        {
            return map[i].msg;
        }
    }
    return NO_MESSAGE;
}

//Gets the last error message of contract if it supports ITV8::IErrorInfoProvider interface, 
//otherwise takes description for err.
//contract - the component returned error.
//err - the error.
inline std::string get_last_error_message(ITV8::IContract* contract, hresult_t err)
{
    IErrorInfoProvider *errprov = 
        ITV8::contract_cast<ITV8::IErrorInfoProvider>(contract);
    return errprov ? errprov->GetErrorMessage() : get_last_error_message(err);
}

//Gets the last error message of contract if it supports ITV8::IErrorInfoProvider interface.
inline std::string get_last_error_message(ITV8::IContract* contract)
{
    return get_last_error_message(contract, ITV8::ENotError);
}

//TODO:Удалить, когда из C# будет приходить "1"/"0" вместо "True"/"False"
inline bool bool_cast(const std::string& value)
{
    if(value == "0" || boost::algorithm::iequals(value, "false"))
    {
        return false;
    }
    else if(value == "1" || boost::algorithm::iequals(value, "true"))
    {
        return true;
    }
    else
    {
        throw std::runtime_error((boost::format("Can't convert \"%1%\" to bool.")%value).str());
    }
}

//Sets the value to adjuster.
//adjuster - the pointer to ITV8::IAsyncAdjuster or ITV8::ISyncAdjuster interface;
//type - the case sensitive name of supported type {bool|int|double|string};
//name - the name of settings property
//value - the value of property in string format. Use "0"/"1" for bool.
//throw exceptions:
//std::runtime_error - if type doesn't support or ITV8::IXXXAdjuster::SetValue return error.
//boost::bad_lexical_cast - if value has invalid format and couldn't be converted to type.
template<class T>
inline void set_value(T* adjuster, const std::string& type, const std::string& name, 
                    const std::string& value)// throw(std::runtime_error)
{
    hresult_t res = ITV8::ENotError;

    if(type == "bool")
    {
        //TODO: Вернуть boost::lexical_cast<::boost>(value), когда будет приходить "1"/"0"
        res = adjuster->SetValue(name.c_str(), bool_cast(value));
    }
    else if(type == "int")
    {
        res = adjuster->SetValue(name.c_str(), boost::lexical_cast< ::int32_t>(value));
    }
    else if(type == "double")
    {
        res = adjuster->SetValue(name.c_str(), boost::lexical_cast<double>(value));
    }
    else if(type == "string" || type == "string[]")
    {
        res = adjuster->SetValue(name.c_str(), value.c_str());
    }
    else
    {
        std::ostringstream str;
        str << "set_value doesn't support type:\""<< type <<"\"" << std::endl;
        
        throw std::runtime_error(str.str());
    }

    if(res != ITV8::ENotError)
    {
        throw std::runtime_error((boost::format(
            "ITV8::IAsyncAdjuster::SetValue<%1%>(\"%2%\", %3%)); return err:%4%; msg:%5%")%
            type%name%value%res%get_last_error_message(adjuster, res)).str());
    }
}

inline boost::any to_any(const std::string& type, const std::string& value)// throw(std::runtime_error)
{
	boost::any result;
	if(type == "bool")
	{
		//TODO: Вернуть boost::lexical_cast<bool>(value), когда будет приходить "1"/"0"
		result = ITV8::bool_cast(value);
	}
	else if(type == "int")
	{
		result = boost::lexical_cast<ITV8::int32_t>(value);
	}
	else if(type == "double")
	{
		result = boost::lexical_cast<ITV8::double_t>(value);
	}
	else if(type == "string")
	{
        result = value;
	}
	else
	{
		throw std::runtime_error((boost::format(
			"Unknown param type in ITV8::to_any(\"%1%\", %2%));") % type % value).str());
	}
	return result;
}

template <typename TAdjuster>
inline void SetBlockConfiguration(TAdjuster* src, bool value)
{
    ITV8::set_value(src, "bool", "blockConfiguration", (value ? "1" : "0"));
}

}

#endif //DEVICEIPINT3_SDKHELPERS_H

