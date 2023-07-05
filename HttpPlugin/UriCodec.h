#ifndef URI_CODEC_H_
#define URI_CODEC_H_

#include <string>

namespace NHttpImpl
{
    std::string UriDecode(const std::string &src);
    std::string UriEncode(const std::string &src);
}

#endif // URI_CODEC_H_