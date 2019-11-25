#include <pulse/simple.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "driver.h"

#define BUFSIZE 1024

//static pa_simple* in;
//static pa_simple* out;

//static const pa_sample_spec ss = { PA_SAMPLE_FLOAT32NE, 48000, 2};

struct PaInfo {
  pa_simple* in;
  pa_simple* out;
};

static pa_simple* pulse_open(m_uint direction, pa_sample_spec* ss) {
  return pa_simple_new(NULL, "Gwion", direction,
    NULL, "Gwion", ss, NULL, NULL, NULL);
}

static m_bool pulse_ini(VM* vm __attribute__((unused)), DriverInfo* di) {
  struct PaInfo* info = (struct PaInfo*)xmalloc(sizeof(struct PaInfo));
  pa_sample_spec ss = { PA_SAMPLE_FLOAT32NE, 48000, 2};
  CHECK_OB((info->out = pulse_open(PA_STREAM_PLAYBACK, &ss)))
  CHECK_OB((info->in  = pulse_open(PA_STREAM_RECORD,   &ss)))
  di->data = info;
  return GW_OK;
}

static void pulse_run(VM* vm, DriverInfo* di) {
  int error;
  struct PaInfo* info = (struct PaInfo*)di->data;
  while(vm->bbq->is_running) {
    m_uint frame, chan;
    float  in_data[BUFSIZE * vm->bbq->nchan];
    float out_data[BUFSIZE * vm->bbq->nchan];
    if(pa_simple_read(info->in, in_data, sizeof(in_data), &error) < 0)
      return;
    for(frame = 0; frame < BUFSIZE; frame++) {
      for(chan = 0; chan < (m_uint)vm->bbq->nchan; chan++)
        vm->bbq->in[chan] = in_data[frame * vm->bbq->nchan + chan];
      di->run(vm);
      for(chan = 0; chan < (m_uint)vm->bbq->nchan; chan++)
        out_data[frame * vm->bbq->nchan + chan] = (float)vm->bbq->out[chan];
      ++vm->bbq->pos;
    }
    if(pa_simple_write(info->out, out_data, sizeof(out_data), &error) < 0)
      return;
  }
  if(pa_simple_drain(info->out, &error) < 0)
    return;
}

static void pulse_del(VM* vm __attribute__((unused)), DriverInfo* di) {
  struct PaInfo* info = (struct PaInfo*)di->data;
  if(info->in)
    pa_simple_free(info->in);
  if(info->out)
    pa_simple_free(info->out);
  free(info);
}

void pulse_driver(Driver* d) {
  d->ini = pulse_ini;
  d->run = pulse_run;
  d->del = pulse_del;
}
