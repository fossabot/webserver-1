#include "GstreamerMeta.h"

#include <boost/asio.hpp>
#include <PtimeFromQword.h>

static const unsigned long long EPOCH = 2208988800ULL;

void fillMeta(GstMetaMarking* meta)
{
    boost::posix_time::ptime t = NMMSS::PtimeFromQword(meta->header.dtTimeBegin);

    using namespace boost::posix_time;
    using namespace boost::gregorian;
    static ptime timet_start(date(1970, 1, 1));
    time_duration diff = t - timet_start;
    timeval tv;
    //drop off the fractional seconds...
    tv.tv_sec = static_cast<decltype(tv.tv_sec)>(diff.ticks() / time_duration::rep_type::res_adjust());
    //The following only works with microsecond resolution!
    tv.tv_usec = static_cast<decltype(tv.tv_usec)>(diff.fractional_seconds());
    
    meta->ntp1 = tv.tv_sec + EPOCH;
    meta->ntp2 = (std::uint32_t)(tv.tv_usec * 4294.967296);
}

GType
gst_meta_marking_api_get_type(void)
{
    static volatile GType type;
    static const gchar *tags[] = { NULL };

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register("GstMetaMarkingAPI", tags);
        g_once_init_leave(&type, _type);
    }
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return type;
}

gboolean
gst_meta_marking_init(GstMeta *meta, gpointer params, GstBuffer *buffer)
{
    GstMetaMarking* marking_meta = (GstMetaMarking*)meta;

    NMMSS::SMediaSampleHeader* h = reinterpret_cast<NMMSS::SMediaSampleHeader*>(params);
    memcpy(&(marking_meta->header), h, sizeof(NMMSS::SMediaSampleHeader));
    fillMeta(marking_meta);

    return TRUE;
}

gboolean
gst_meta_marking_transform(GstBuffer *dest_buf,
    GstMeta *src_meta,
    GstBuffer *src_buf,
    GQuark type,
    gpointer data) 
{
    GstMetaMarking* src_meta_marking = (GstMetaMarking*)src_meta;
    GST_META_MARKING_ADD(dest_buf, &(src_meta_marking->header));

    return TRUE;
}

void
gst_meta_marking_free(GstMeta *meta, GstBuffer *buffer) {
}

const GstMetaInfo *
gst_meta_marking_get_info(void)
{
    static const GstMetaInfo *meta_info = NULL;

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *meta =
            gst_meta_register(gst_meta_marking_api_get_type(), "GstMetaMarking",
                sizeof(GstMetaMarking), (GstMetaInitFunction)gst_meta_marking_init,
                (GstMetaFreeFunction)gst_meta_marking_free, (GstMetaTransformFunction)gst_meta_marking_transform);
        g_once_init_leave(&meta_info, meta);
    }
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return meta_info;
}
