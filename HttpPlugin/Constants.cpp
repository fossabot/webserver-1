#include "Constants.h"

const char* const BOUNDARY_HEADER = "ngpboundary";
const char* const BOUNDARY = "--ngpboundary";
const char* const HOST_PREFIX = "hosts/";
const char* const HOST_AGENT = "HostAgent/HostAgent";
const char* const DICTIONARY = "ExecutionManager/Dictionary";
const char* const LIMIT_MASK = "limit";
const char* const SCALE_MASK = "scale";
const char* const OFFSET_MASK = "offset";

const char* const ENDPOINT_TEMPLATE = "^/([^/]*/[^/]*/[^/]*)";

const char* const WIDTH_PARAMETER = "w";
const char* const HEIGHT_PARAMETER = "h";

const char* const FORMAT_PARAMETER = "format";
const char* const FORMAT_VALUE_MJPEG = "mjpeg";

const char* const FORMAT_VALUE_RTSP = "rtsp";
const char* const FORMAT_VALUE_HLS = "hls";
const char* const FORMAT_VALUE_MP4 = "mp4";

const char* const EXPORT_FORMAT_JPG = "jpg";
const char* const EXPORT_FORMAT_PDF = "pdf";
const char* const EXPORT_FORMAT_MKV = "mkv";
const char* const EXPORT_FORMAT_AVI = "avi";
const char* const EXPORT_FORMAT_EXE = "exe";
const char* const EXPORT_FORMAT_MP4 = "mp4";

const char* const STREAM_ID_PARAMETER = "stream_id";

const char* const PARAM_ENABLE_TOKEN_AUTH = "enable_token_auth";
const char* const PARAM_TOKEN_VALID_HOURS = "valid_token_hours";

const char* const PARAM_KEEP_ALIVE = "keep_alive";
const char* const PARAM_HLS_TIME = "hls_time";
const char* const PARAM_HLS_LIST_SIZE = "hls_list_size";
const char* const PARAM_HLS_WRAP = "hls_wrap";

const char* const RESULT_MASK = "result";

const char* const RESULT_TYPE_PARAM = "result_type";
const char* const RESULT_TYPE_FULL = "full";

const char* const ACCURACY_PARAM = "accuracy";

const float MIN_ACCURACY = 0.0f;
const float MAX_ACCURACY = 1.0f;
const float DEFAULT_ACCURACY = 0.9f;

const char* const FRAMERATE_PARAMETER = "fr";
const float FRAMERATE_DEFAULT_VALUE = -1;

const char* const PARAM_ARCHIVE = "archive";
const char* const PARAM_DETECTOR = "detector";

const char* const RTSP_PROXY_PATH = "/rtspproxy";

const char* const TOKEN_AUTH_SESSION_ID = "TOKEN_AUTH_SESSION_ID";

const char* const KEY_FRAMES_PARAMETER = "key_frames";

const char* const EVENT_STREAM_TYPE = "text/event-stream";
const char* const CONNECTION_HEADER = "Connection";
const char* const CONNECTION_TYPE = "keep-alive";

const char* const CONTENT_TYPE = "Content-Type: ";
const char* const CONTENT_LENGTH = "Content-Length: ";
const char* const AXXON_AUTHORIZATION = "X-AxxonAuthorization";

const std::uint8_t CR = 0x0D;
const std::uint8_t LF = 0x0A;

const uint32_t SAMPLE_TIMEOUT = 60000;
const uint32_t KEY_SAMPLE_TIMEOUT = 120000;
