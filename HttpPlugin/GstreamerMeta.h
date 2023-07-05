#ifndef GSTREAMER_META_H__
#define GSTREAMER_META_H__

#include <cstdint>
#include <gst/gst.h>
#include <gst/gstmeta.h>

#include <Sample.h>

typedef struct _GstMetaMarking GstMetaMarking;

struct _GstMetaMarking {
    GstMeta meta;
    NMMSS::SMediaSampleHeader header;
    std::uint32_t ntp1;
    std::uint32_t ntp2;
};

GType gst_meta_marking_api_get_type(void);
const GstMetaInfo* gst_meta_marking_get_info(void);
#define GST_META_MARKING_GET(buf) ((GstMetaMarking *)gst_buffer_get_meta(buf,gst_meta_marking_api_get_type()))
#define GST_META_MARKING_ADD(buf, header) ((GstMetaMarking *)gst_buffer_add_meta(buf,gst_meta_marking_get_info(),(gpointer)header))

#endif // GSTREAMER_META_H__
