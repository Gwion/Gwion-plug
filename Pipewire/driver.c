#define _GNU_SOURCE
#include <regex.h>
#include <spa/utils/result.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/latency-utils.h>
#include <pipewire/pipewire.h>

#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "gwion.h"
#include "shreduler_private.h"

#include "instr.h"
#include "operator.h"
#include "object.h"
#include "import.h"

#include "gack.h"

#define BUFFER_SIZE 1024

#ifndef USE_DOUBLE
#define GW_FORMAT SPA_AUDIO_FORMAT_F32
#else
#define GW_FORMAT SPA_AUDIO_FORMAT_F64
#endif

struct node_info {
  m_str category;
  m_str name;
  struct pw_loop *loop;
  const struct pw_stream_events *events;
  void *data;
  int direction;
  enum pw_stream_flags flags;
  uint32_t sr;
};
struct node_data {
  struct pw_stream *stream;
  m_float **buf;
  uint8_t *buffer;
};

// TODO: error msgs
static bool new_node_data(MemPool mp, struct node_data *node,
    const struct node_info *info, const uint16_t n) {
  if(!(node->buf = mp_calloc2(mp, n * sizeof(m_float*)))) return false;
  if(!(node->buffer = mp_malloc2(mp, BUFFER_SIZE))) return false;
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(node->buffer, BUFFER_SIZE);
  struct pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                        PW_KEY_MEDIA_CATEGORY, info->category,
                        PW_KEY_MEDIA_ROLE, "Music",
                        NULL);
  if(!props) {
    gw_err("{R}Pipewire{0} can't create %s properties\n", info->name);
    return false;
  }
  if(!(node->stream = pw_stream_new_simple(
    info->loop, info->name, props,
    info->events, info->data))) {
    gw_err("{R}Pipewire{0} can't create %s stream\n", info->name);
    const char *msg = NULL;
    pw_stream_get_state(node->stream, &msg);
    gw_err("{R}Pipewire{0} %s\n", msg);
    return false;
  }
  const struct spa_pod *params[1]; 
  if(!(params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
                        &SPA_AUDIO_INFO_RAW_INIT(
                        .rate = info->sr,
                        .format = GW_FORMAT,
                        .channels = n)))) {
    gw_err("{R}Pipewire{0} can't create %s format\n", info->name);
    return false;
  }
  if(pw_stream_connect(node->stream,
                  info->direction,
                  PW_ID_ANY,
                  info->flags |
                  PW_STREAM_FLAG_AUTOCONNECT |
                  PW_STREAM_FLAG_MAP_BUFFERS |
                  PW_STREAM_FLAG_RT_PROCESS,
                  params, 1) < 0) {
    gw_err("{R}Pipewire{0} can't connect %s stream\n", info->name);
    const char *msg = NULL;
    pw_stream_get_state(node->stream, &msg);
    gw_err("{R}Pipewire{0} %s\n", msg);
    return false;
  }
  return true;
}

static void free_node_data(MemPool mp, struct node_data *info, const int16_t n) {
  if(info->stream) {
    pw_stream_disconnect(info->stream);
    pw_stream_destroy(info->stream);
  }
  if(info->buf) mp_free2(mp, n * sizeof(m_float*), info->buf);
  if(info->buffer) mp_free2(mp, BUFFER_SIZE, info->buffer);
}

struct drv_data {
  struct node_data in;
  struct node_data out;
  VM* vm;
  struct pw_main_loop *loop;
};

static void do_quit(void *userdata, int signal_number NUSED) {
  struct drv_data* data = (struct drv_data*)userdata;
  data->vm->bbq->is_running = 0;
  pw_main_loop_quit(data->loop);
}
/*
  for(uint8_t j = 0; j < bbq->si->in; j++)
    data->in[j] = pw_filter_get_dsp_buffer(data->in_port[j], n_samples);
  for(uint8_t j = 0; j < bbq->si->out; j++)
    data->out[j] = pw_filter_get_dsp_buffer(data->out_port[j], n_samples);
  for(uint32_t i = 0; i < n_samples; i++) {
    for(uint8_t j = 0; j < bbq->si->in; j++)
      bbq->in[j] = data->in[j] ? data->in[j][i] : 0.0;
    vm->bbq->run(vm);
    for(uint8_t j = 0; j < bbq->si->out; j++)
      if(data->out[j]) data->out[j][i] = bbq->out[j];
    next_bbq_pos(vm);
  }
}
*/
ANN static void in_process(void *userdata) {
  struct drv_data *data = userdata;
  const Driver* bbq = data->vm->bbq;
  if(!bbq->is_running) {
    pw_main_loop_quit(data->loop);
    return;
  }
  pw_stream_trigger_process(data->out.stream);
}

#define dequeue(target)                                                         \
  struct pw_buffer *target##_b = pw_stream_dequeue_buffer(data->target.stream); \
  if(!target##_b) return pw_log_warn("out of %s buffers", #target);             \
  const int target##_stride = sizeof(m_float) * bbq->si->target;                \
  const int target##_frames = SPA_MIN(target##_b->requested,                    \
      target##_b->buffer->datas[0].maxsize / target##_stride);                  \
  m_float *target##s = target##_b->buffer->datas[0].data;

#define queue(target)                                                           \
  target##_b->buffer->datas[0].chunk->offset = 0;                               \
  target##_b->buffer->datas[0].chunk->stride = target##_stride;                 \
  target##_b->buffer->datas[0].chunk->size = target##_frames* target##_stride;  \
  pw_stream_queue_buffer(data->target.stream, target##_b);

ANN static void out_process(void *userdata) {
  struct drv_data *data = userdata;
  const Driver *bbq = data->vm->bbq;
  dequeue(in);
  dequeue(out);
  for(int i = 0; i < out_frames; i++) {
    for(int j = 0; j < bbq->si->in; j++)
      bbq->in[j] = *ins++;
    bbq->run(data->vm);
    for(int j = 0; j < bbq->si->out; j++)
      *outs++ = bbq->out[j];
  }
  queue(in);
  queue(out);
}

static const struct pw_stream_events in_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = in_process,
};

static const struct pw_stream_events out_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = out_process,
};

static bool pw_ini(VM* vm, Driver* di) {
  const MemPool mp = vm->gwion->mp;
  int argc = 0; char **argv = NULL;
  struct drv_data *data = mp_malloc2(mp, sizeof(struct drv_data));
  di->driver->data = data;
  data->vm = vm;
  pw_init(&argc, &argv);
  data->loop = pw_main_loop_new(NULL);
  struct pw_loop *loop = pw_main_loop_get_loop(data->loop);
  pw_loop_add_signal(loop, SIGINT, do_quit, data);
  pw_loop_add_signal(loop, SIGTERM, do_quit, data); 

  struct node_info in_info = {
    .category = "Capture",
    .loop = loop,
    .flags = PW_STREAM_FLAG_DRIVER,
    .name = "Gwion-in",
    .events = &in_stream_events,
    .direction = PW_DIRECTION_INPUT,
    .sr = di->si->sr,
    .data = data,
  };
  if(!new_node_data(mp, &data->in, &in_info, di->si->in)) return false;

  struct node_info out_info = {
    .category = "Playback",
    .loop = loop,
    .flags = PW_STREAM_FLAG_TRIGGER,
    .name = "Gwion-out",
    .events = &out_stream_events,
    .direction = PW_DIRECTION_OUTPUT,
    .sr = di->si->sr,
    .data = data,
  };
  if(!new_node_data(mp, &data->out, &out_info, di->si->out)) return false;
  return true;
}

static void pw_run(VM* vm NUSED, Driver* di) {
  struct drv_data *data = (struct drv_data*)di->driver->data;
  pw_main_loop_run(data->loop);
}

static void pw_del(VM* vm __attribute__((unused)), Driver* di) {
  struct drv_data* data = (struct drv_data*)di->driver->data;
  if(!data) return;
  MemPool mp = vm->gwion->mp;
  free_node_data(mp, &data->in, di->si->in);
  free_node_data(mp, &data->out, di->si->out);
  if(data->loop) pw_main_loop_destroy(data->loop);
  mp_free2(mp, sizeof(struct drv_data), data);
  pw_deinit();
}

GWDRIVER(Pipewire) {
  d->ini = pw_ini;
  d->run = pw_run;
  d->del = pw_del;
}

