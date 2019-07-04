
#include <stdlib.h>
#include <jack/jack.h>

#include <unistd.h> // for sleep

#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "vm.h"
#include "env.h"
#include "gwion.h"
#include "driver.h"
#include "plug.h"

struct JackInfo {
  jack_port_t** iport;
  jack_port_t** oport;
  jack_client_t* client;
  VM* vm;
};

static void gwion_shutdown(void *arg) {
  VM *vm = (VM *)arg;
  vm->bbq->is_running = 0;
}

static void inner_cb(struct JackInfo* info, jack_default_audio_sample_t** in,
    jack_default_audio_sample_t** out, jack_nframes_t nframes) {
  jack_nframes_t frame;
  VM* vm = info->vm;
  m_uint chan;
  for(frame = 0; frame < nframes; frame++) {
    for(chan = 0; chan < vm->bbq->si->in; chan++)
      vm->bbq->in[chan] = in[chan][frame];
    vm->bbq->run(vm);
    for(chan = 0; chan < (m_uint)vm->bbq->si->out; chan++)
      out[chan][frame] = vm->bbq->out[chan];
    ++vm->bbq->pos;
  }
}

static int gwion_cb(jack_nframes_t nframes, void *arg) {
  m_uint chan;
  struct JackInfo* info = (struct JackInfo*)arg;
  VM* vm  = info->vm;
  jack_default_audio_sample_t  * in[vm->bbq->si->in];
  jack_default_audio_sample_t  * out[vm->bbq->si->out];
  for(chan = 0; chan < vm->bbq->si->in; chan++)
    in[chan] = jack_port_get_buffer(info->iport[chan], nframes);
  for(chan = 0; chan < (m_uint)vm->bbq->si->out; chan++)
    out[chan] = jack_port_get_buffer(info->oport[chan], nframes);
  inner_cb(info, in, out, nframes);
  return 0;
}

static m_bool init_client(VM* vm, struct JackInfo* info) {
  jack_status_t status;
  info->client = jack_client_open("Gwion", JackNullOption, &status, NULL);
  if(!info->client) {
    gw_err("jack_client_open() failed, status = 0x%2.0x\n", status);
    if(status & JackServerFailed)
      gw_err("Unable to connect to JACK server\n");
    return GW_ERROR;
  }
  info->vm = vm;
  jack_set_process_callback(info->client, gwion_cb, info);
  jack_on_shutdown(info->client, gwion_shutdown, vm);
  return GW_OK;
}

static m_bool set_chan(struct JackInfo* info, m_uint nchan, m_bool input) {
  char chan_name[50];
  m_uint chan;
  jack_port_t** port = input ? info->iport : info->oport;
  for(chan = 0; chan < nchan; chan++) {
    sprintf(chan_name, input ? "input_%ld" : "output_%ld", chan);
    if(!(port[chan] = jack_port_register(info->client, chan_name,
        JACK_DEFAULT_AUDIO_TYPE, input ?
        JackPortIsInput : JackPortIsOutput , chan))) {
      gw_err("no more JACK %s ports available\n", input ?
          "input" : "output");
      return GW_ERROR;
    }
  }
  return GW_OK;
}

static m_bool jack_ini(VM* vm, Driver* di) {
  struct JackInfo* info = (struct JackInfo*)xmalloc(sizeof(struct JackInfo));
  info->iport = (jack_port_t**)xmalloc(sizeof(jack_port_t *) * di->si->in);
  info->oport = (jack_port_t**)xmalloc(sizeof(jack_port_t *) * di->si->out);
  CHECK_BB(init_client(vm, info))
  CHECK_BB(set_chan(info, di->si->out, 0))
  CHECK_BB(set_chan(info, di->si->in,  1))
  di->si->sr = jack_get_sample_rate(info->client);
  di->driver->data = info;
  return GW_OK;
}

static m_bool connect_ports(struct JackInfo* info, const char** ports,
    m_uint nchan, m_bool input) {
  m_uint chan;
  jack_port_t** jport = input ? info->iport : info->oport;
  for(chan = 0; chan < nchan; chan++) {
    const char* l = input ? ports[chan] : jack_port_name(jport[chan]);
    const char* r = input ? jack_port_name(jport[chan]) : ports[chan];
    if(jack_connect(info->client, l, r)) {
      gw_err("cannot connect %s ports\n", input ? "input" : "output");
      return GW_ERROR;
    }
  }
  return GW_OK;
}

static m_bool init_ports(struct JackInfo* info, m_uint nchan, m_bool input) {
  m_bool ret;
  const char** ports = jack_get_ports(info->client, NULL, NULL,
      JackPortIsPhysical | (input ? JackPortIsOutput : JackPortIsInput));
  if(!ports)
    return GW_ERROR;
  ret = connect_ports(info, ports, nchan, input);
  free(ports);
  return ret;
}

static void jack_run(VM* vm, Driver* di) {
  struct JackInfo* info = (struct JackInfo*)di->driver->data;
  if(jack_activate(info->client)) {
    gw_err("cannot activate client\n");
    return;
  }
  if(init_ports(info, di->si->out, 0) < 0 || init_ports(info, di->si->in,  1) < 0)
    return;
  while(vm->bbq->is_running)
    usleep(10);
}

static void jack_del(VM* vm __attribute__((unused)), Driver* di) {
  struct JackInfo* info = (struct JackInfo*)di->driver->data;
  jack_deactivate(info->client);
  jack_client_close(info->client);
  free(info->iport);
  free(info->oport);
}

//void jack_driver(Driver* d) {
GWMODSTR(jack)
GWDRIVER(jack) {
  d->ini = jack_ini;
  d->run = jack_run;
  d->del = jack_del;
}
