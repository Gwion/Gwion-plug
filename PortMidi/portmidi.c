/* TODO:
  put MSG only in midiin
*/

#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <portmidi.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "instr.h"
#include "gwion.h"
#include "object.h"
#include "plug.h"
#include "operator.h"
#include "import.h"


static Map map = NULL;
typedef struct
{
  m_float last;
  PmStream* stream;
  VM* vm;
  Vector client;
  pthread_t thread;
  pthread_mutex_t mutex;
} MidiInfo;

static MidiInfo* get(VM* vm, m_uint i)
{
  if(!map)
  {
    map = new_map(vm->gwion->mp);
    Pm_Initialize();
  }
  MidiInfo* info = (MidiInfo*)map_get(map, (vtype)i+1);
  if(!info)
  {
    info = (MidiInfo*)xmalloc(sizeof(MidiInfo));
    info->stream = NULL;
    info->client = new_vector(vm->gwion->mp);
    info->thread = 0;
    info->vm = vm;
    map_set(map, i+1, (vtype)info);
  }
  return info;
}

static void release_info(MemPool p, m_uint i, M_Object o)
{
  MidiInfo* info = (MidiInfo*)map_get(map, (vtype)i+i);
  if(info)
  {
    vector_rem(info->client, vector_find(info->client, (vtype)o));
    if(!vector_size(info->client))
    {
      if(info->thread)
        pthread_cancel(info->thread);
      Pm_Close(info->stream);
      free_vector(p, info->client);
      xfree(info);
    }
  }
  if(!map_size(map))
  {
    free_map(p, map);
    map = NULL;
    Pm_Terminate();
  }
}

static m_uint active = 0;
static m_int o_pm_stream;
static m_int o_pm_id;
static m_int o_pm_status;
static m_int o_pm_data1;
static m_int o_pm_data2;
static m_int o_pm_msg;

#define STREAM(o) *(PmStream**)(o->data + o_pm_stream)
#define ID(o)     *(m_int*)    (o->data + o_pm_id)
#define STATUS(o) *(m_uint*)   (o->data + o_pm_status)
#define DATA1(o) *(m_uint*)    (o->data + o_pm_data1)
#define DATA2(o) *(m_uint*)    (o->data + o_pm_data2)
#define MSG(o) *(Vector*)      (o->data + o_pm_msg)

static MFUN(pm_name)
{
  const PmDeviceInfo* info = Pm_GetDeviceInfo(ID(o));
  if(!info)
    *(m_uint*)RETURN = (m_uint)new_string(shred->info->mp, shred, "no device");
  else
    *(m_uint*)RETURN = (m_uint)new_string(shred->info->mp, shred, (m_str)info->name);
}

static SFUN(pm_error)
{
  *(m_uint*)RETURN = (m_uint)new_string(shred->info->mp, shred, (m_str)Pm_GetErrorText(*(m_int*)MEM(0)));
}

static MFUN(pm_close)
{
  release_info(shred->info->mp, ID(o), o);
  STREAM(o) = NULL;
  ID(o) = -1;
  *(m_uint*)RETURN = 1;
}

static CTOR(pm_ctor)
{
  ID(o) = -1;
  MSG(o) = new_vector(shred->info->mp);
}

static DTOR(pm_dtor)
{
  free_vector(shred->info->mp, MSG(o));
  if(get(shred->info->vm, ID(o)))
    release_info(shred->info->mp, ID(o), o);
}

static MFUN(midiout_open)
{
  if(ID(o) > -1)
    release_info(shred->info->mp, ID(o), o);
  ID(o) = *(m_uint*)MEM(SZ_INT);
  MidiInfo* info = get(shred->info->vm, ID(o));
  vector_add(info->client, (vtype)o);
  ++o->ref;
  if(!info->stream)
    Pm_OpenOutput(&info->stream, ID(o), 0, 0, NULL, NULL, 0);
  STREAM(o) = info->stream;
  *(m_uint*)RETURN = 1;
}

static MFUN(midiout_send_self)
{
  *(m_uint*)RETURN = Pm_WriteShort(STREAM(o), 0, 
    Pm_Message(STATUS(o), DATA1(o), DATA2(o)));
}

static MFUN(midiout_send)
{
  *(m_uint*)RETURN = Pm_WriteShort(STREAM(o), 0, 
    Pm_Message(*(m_uint*)MEM(SZ_INT), *(m_uint*)MEM(SZ_INT*2), *(m_uint*)MEM(SZ_INT*3)));
}

static void* pm_recv(void* data)
{
  m_uint i;
  PmEvent event;
  MidiInfo* info = (MidiInfo*)data;
  m_float now = info->vm->bbq->pos;
  while(1)
  {
    while(Pm_Poll(info->stream) == 1)
    {
      Pm_Read(info->stream, &event, 1);
      for(i = 0; i < vector_size(info->client); i++) {
        M_Object o = (M_Object)vector_at(info->client, i);
        pthread_mutex_lock(&info->mutex);
        vector_add(MSG(o), (vtype)(m_uint)event.message);
        pthread_mutex_unlock(&info->mutex);
        if(info->last != now);
        broadcast(o);
      }
    }
    info->last = now;
    usleep(10000); // 1/10th of a millisecond
  }
  return NULL;
}

static MFUN(midiin_open)
{
  ID(o) = *(m_uint*)MEM(SZ_INT);
  MidiInfo* info = get(shred->info->vm, ID(o));
  if(!info->thread)
  {
    Pm_OpenInput(&info->stream, ID(o), 0, 0, NULL, NULL);
    pthread_mutex_init(&info->mutex, NULL);
    pthread_create(&info->thread, NULL, pm_recv, info);
  }
  vector_add(info->client, (vtype)o);
  STREAM(o) = info->stream;
}

static MFUN(midiin_recv)
{
  MidiInfo* info = get(shred->info->vm, ID(o));
  pthread_mutex_lock(&info->mutex);
  *(m_uint*)RETURN = vector_size(MSG(o)) ? 1 : 0;
  pthread_mutex_unlock(&info->mutex);
}

static MFUN(midiin_read)
{
  MidiInfo* info = get(shred->info->vm, ID(o));
  pthread_mutex_lock(&info->mutex);
  m_uint msg = (m_uint)vector_front(MSG(o));
  STATUS(o) = Pm_MessageStatus(msg);
  DATA1(o)  = Pm_MessageData1(msg);
  DATA2(o)  = Pm_MessageData2(msg);
  vector_rem(MSG(o), (vtype)0);
  pthread_mutex_unlock(&info->mutex);
}

GWION_IMPORT(portmidi) {
  const Type t_portmidi = gwi_class_ini(gwi, "PortMidi", "Event");
  SET_FLAG(t_portmidi, abstract);
  gwi_class_xtor(gwi, pm_ctor, pm_dtor);
  gwi_item_ini(gwi, "@internal", "@dummy");
   gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "@internal", "@stream");
  o_pm_stream = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_BB(o_pm_stream)
  gwi_item_ini(gwi,"int",  "id");
  o_pm_id = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_BB(o_pm_id)
  gwi_item_ini(gwi,"int",  "status");
  o_pm_status  = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_BB(o_pm_status)
  gwi_item_ini(gwi,"int",  "data1");
  o_pm_data1 = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_BB(o_pm_data1)
  gwi_item_ini(gwi,"int",  "data2");
  o_pm_data2 = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_BB(o_pm_data2)
  gwi_item_ini(gwi, "@internal", "@msg");
  o_pm_msg = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_BB(o_pm_msg)
  gwi_func_ini(gwi, "string", "name");
  GWI_BB(gwi_func_end(gwi, pm_name, ae_flag_none))
  gwi_func_ini(gwi, "string", "error");
    gwi_func_arg(gwi, "int", "id");
  GWI_BB(gwi_func_end(gwi, pm_error, ae_flag_static))
  gwi_func_ini(gwi, "int", "close");
  GWI_BB(gwi_func_end(gwi, pm_close, ae_flag_none))
  GWI_BB(gwi_class_end(gwi))

  GWI_BB(gwi_class_ini(gwi, "MidiOut", "PortMidi"))
  gwi_func_ini(gwi, "int", "open");
    gwi_func_arg(gwi, "int", "id");
  GWI_BB(gwi_func_end(gwi, midiout_open, ae_flag_none))
  gwi_func_ini(gwi, "int", "send");
  GWI_BB(gwi_func_end(gwi, midiout_send_self, ae_flag_none))
  gwi_func_ini(gwi, "int", "send");
    gwi_func_arg(gwi, "int", "status");
    gwi_func_arg(gwi, "int", "data1");
    gwi_func_arg(gwi, "int", "data2");
  GWI_BB(gwi_func_end(gwi, midiout_send, ae_flag_none))
  GWI_BB(gwi_class_end(gwi))

  GWI_BB(gwi_class_ini(gwi, "MidiIn", "PortMidi"))
  gwi_func_ini(gwi, "int", "open");
    gwi_func_arg(gwi, "int", "id");
  GWI_BB(gwi_func_end(gwi, midiin_open, ae_flag_none))
  gwi_func_ini(gwi, "int", "recv");
  GWI_BB(gwi_func_end(gwi, midiin_recv, ae_flag_none))
  gwi_func_ini(gwi, "int", "read");
  GWI_BB(gwi_func_end(gwi, midiin_read, ae_flag_none))
  GWI_BB(gwi_class_end(gwi))

  return GW_OK;
}
