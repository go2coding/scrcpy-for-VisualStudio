#include "frame_source.h"

void
sc_frame_source_init(struct sc_frame_source *source) {
    source->sink_count = 0;
}

void
sc_frame_source_add_sink(struct sc_frame_source *source,
                         struct sc_frame_sink *sink) {
    assert(source->sink_count < SC_FRAME_SOURCE_MAX_SINKS);
    assert(sink);
    assert(sink->ops);
    source->sinks[source->sink_count++] = sink;
}

static void
sc_frame_source_sinks_close_firsts(struct sc_frame_source *source,
                                    unsigned count) {
    while (count) {
        struct sc_frame_sink *sink = source->sinks[--count];
        sink->ops->close(sink);
    }
}

bool
sc_frame_source_sinks_open(struct sc_frame_source *source,
                           const AVCodecContext *ctx) {
    assert(source->sink_count);
    for (unsigned i = 0; i < source->sink_count; ++i) {
        struct sc_frame_sink *sink = source->sinks[i];
        if (!sink->ops->open(sink, ctx)) {
            sc_frame_source_sinks_close_firsts(source, i);
            return false;
        }
    }

    return true;
}

void
sc_frame_source_sinks_close(struct sc_frame_source *source) {
    assert(source->sink_count);
    sc_frame_source_sinks_close_firsts(source, source->sink_count);
}

bool
sc_frame_source_sinks_push(struct sc_frame_source *source,
                            const AVFrame *frame) {
    assert(source->sink_count);
    for (unsigned i = 0; i < source->sink_count; ++i) {
        struct sc_frame_sink *sink = source->sinks[i];
        if (!sink->ops->push(sink, frame)) {
            return false;
        }
    }

    return true;
}
