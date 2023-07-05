#ifndef HTTP_PLUGIN_CONSTANTS_H__
#define HTTP_PLUGIN_CONSTANTS_H__

#include <cstdint>

extern const char* const BOUNDARY_HEADER;
extern const char* const BOUNDARY;
extern const char* const HOST_PREFIX;
extern const char* const HOST_AGENT;
extern const char* const DICTIONARY;
extern const char* const LIMIT_MASK;
extern const char* const SCALE_MASK;
extern const char* const OFFSET_MASK;

extern const char* const ENDPOINT_TEMPLATE;

extern const char* const WIDTH_PARAMETER;
extern const char* const HEIGHT_PARAMETER;

extern const char* const FORMAT_PARAMETER;
extern const char* const FORMAT_VALUE_MJPEG;
extern const char* const FORMAT_VALUE_HLS;
extern const char* const FORMAT_VALUE_RTSP;
extern const char* const FORMAT_VALUE_MP4;

extern const char* const EXPORT_FORMAT_JPG;
extern const char* const EXPORT_FORMAT_PDF;
extern const char* const EXPORT_FORMAT_MKV;
extern const char* const EXPORT_FORMAT_AVI;
extern const char* const EXPORT_FORMAT_EXE;
extern const char* const EXPORT_FORMAT_MP4;

extern const char* const STREAM_ID_PARAMETER;

extern const char* const PARAM_ENABLE_TOKEN_AUTH;
extern const char* const PARAM_TOKEN_VALID_HOURS;

extern const char* const PARAM_KEEP_ALIVE;
extern const char* const PARAM_HLS_TIME;
extern const char* const PARAM_HLS_LIST_SIZE;
extern const char* const PARAM_HLS_WRAP;

extern const char* const RESULT_MASK;

extern const char* const RESULT_TYPE_PARAM;
extern const char* const RESULT_TYPE_FULL;

extern const char* const ACCURACY_PARAM;
extern const float MIN_ACCURACY;
extern const float MAX_ACCURACY;
extern const float DEFAULT_ACCURACY;

extern const char* const FRAMERATE_PARAMETER;
extern const float FRAMERATE_DEFAULT_VALUE;

extern const char* const PARAM_ARCHIVE;
extern const char* const PARAM_DETECTOR;

extern const char* const RTSP_PROXY_PATH;

extern const char* const TOKEN_AUTH_SESSION_ID;

extern const char* const KEY_FRAMES_PARAMETER;

extern const char* const EVENT_STREAM_TYPE;
extern const char* const CONNECTION_HEADER;
extern const char* const CONNECTION_TYPE;

extern const char* const CONTENT_TYPE;
extern const char* const CONTENT_LENGTH;
extern const char* const AXXON_AUTHORIZATION;

extern const std::uint8_t CR;
extern const std::uint8_t LF;

extern const uint32_t SAMPLE_TIMEOUT;
extern const uint32_t KEY_SAMPLE_TIMEOUT;

#endif // HTTP_PLUGIN_CONSTANTS_H__
