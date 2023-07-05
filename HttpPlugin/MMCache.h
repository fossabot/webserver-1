#pragma once

#include <memory>

#include <mmss/MMSS.h>
#include <Logging/log2.h>

namespace NCorbaHelpers
{
	class IContainer;
}

namespace NHttp
{
	struct IMMOrigin
	{
		virtual ~IMMOrigin() {}
		virtual NMMSS::IPullStyleSource* GetSource() = 0;
	};
	using PMMOrigin = std::shared_ptr<IMMOrigin>;

	struct IMMCache
	{
		virtual ~IMMCache() {}
		virtual PMMOrigin GetMMOrigin(const char* const, bool) = 0;
	};
	using PMMCache = std::shared_ptr<IMMCache>;

	PMMCache CreateMMCache(NCorbaHelpers::IContainer*);
}
