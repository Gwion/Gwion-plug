#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "instr.h"
#include "gwion.h"
#include "object.h"
#include "operator.h"
#include "import.h"
#include "shreduler_private.h"
#include "gw_evdev.h"

#define EVDEV_PREFIX "/dev/input/event"

#define INFO(o) (EvdevInfo*)(o->data + SZ_INT*2)

static DTOR(evdev_dtor) {
  EvdevInfo* info = INFO(o);
  if(info->thread) {
    pthread_cancel(info->thread);
    pthread_join(info->thread, NULL);
    pthread_mutex_destroy(&info->mutex);
    close(info->fd);
  }
  free_inputeventlist(shred->info->mp, info->args);
}

ANN static void* evdev_process(void* arg) {
  int rc;
  struct input_event event;
  const M_Object o = (M_Object)arg;
  EvdevInfo* info = INFO(o);
  struct pollfd curr = { .fd = info->fd, .events = POLL_IN };
  const int flag = LIBEVDEV_READ_FLAG_BLOCKING;
  do {
    poll(&curr, 1, -1);
    rc = libevdev_next_event(info->evdev, flag, &event);
    if(rc == LIBEVDEV_READ_STATUS_SYNC) {
      do rc = libevdev_next_event(info->evdev, LIBEVDEV_READ_FLAG_FORCE_SYNC, &event);
      while(rc == LIBEVDEV_READ_STATUS_SYNC);
    }
    if(rc == LIBEVDEV_READ_STATUS_SUCCESS  && event.type) {
      pthread_mutex_lock(&info->mutex);
      inputeventlist_add(info->mp, &info->args, event);
      broadcast(o);
      pthread_mutex_unlock(&info->mutex);
    }
  } while((rc == 1 || rc == 0 || rc == -11));
  return NULL;
}

static MFUN(evdev_index) {
  EvdevInfo* info = INFO(o);
  info->evdev = libevdev_new();
  info->args  = new_inputeventlist(shred->info->mp, 0);
  info->mp = shred->info->mp;
  const m_int index = *(m_int*)MEM(SZ_INT);
  char c[strlen(EVDEV_PREFIX) + num_digit(index) + 1];
  sprintf(c, "%s%"INT_F, EVDEV_PREFIX, index);
  *(M_Object*)RETURN = o;
  if((info->fd = open((const char*)c, O_RDONLY)) == -1) {
    xfun_handle(shred, "EvdevCreateException");
    return;
  }
  libevdev_set_fd(info->evdev, info->fd);
  info->index = index;
  struct input_event event;
  while(libevdev_has_event_pending(info->evdev))
    libevdev_next_event(info->evdev, LIBEVDEV_READ_FLAG_NORMAL, &event);
  pthread_mutex_init(&info->mutex, NULL);
  pthread_create(&info->thread, NULL, evdev_process, o);
}

static MFUN(evdev_get_repeat) {
  EvdevInfo* info = INFO(o);
  int delay, period;
  libevdev_get_repeat(info->evdev, &delay, &period);
  **(m_int**)MEM(SZ_INT) = delay;
  **(m_int**)MEM(SZ_INT) = period;
}

static MFUN(evdev_recv) {
  EvdevInfo* info = INFO(o);
  M_Object ev = *(M_Object*)MEM(SZ_INT);
  pthread_mutex_lock(&info->mutex);
  InputEventList *const v = info->args;
  if(info->idx == v->len) {
    info->idx = v->len = 0;
    *(m_uint*)RETURN = 0;
    pthread_mutex_unlock(&info->mutex);
    return;
  }
  struct input_event arg = inputeventlist_at(v, info->idx++);
  gw_input_event *e = EVDEVEV(ev);
  e->type  = arg.type;
  e->code  = arg.code;
  e->value = arg.value;
  e->sec   = arg.time.tv_sec;
  e->usec  = arg.time.tv_usec;
  *(m_uint*)RETURN = 1;
  pthread_mutex_unlock(&info->mutex);
}

static MFUN(evdev_get_index) {
  const EvdevInfo* info = INFO(o);
  *(m_uint*)RETURN = info->index;
}

#define describe_var(func)                       \
static MFUN(evdev_##func) {                      \
  const EvdevInfo* info = INFO(o);               \
  const struct libevdev* dev = info->evdev;      \
  m_str str = (m_str)libevdev_get_##func(dev);   \
  *(M_Object*)RETURN  = new_string(shred->info->vm->gwion, str);  \
}                                                \
static MFUN(evdev_set_##func) {                  \
  const EvdevInfo* info = INFO(o);               \
  const M_Object obj = *(M_Object*)MEM(SZ_INT);  \
  libevdev_set_##func(info->evdev, STRING(obj)); \
  *(M_Object*)RETURN  = obj;                     \
}

#define import_var(func)                                         \
  GWI_B(gwi_func_ini(gwi, "string", #func))\
  GWI_B(gwi_func_end(gwi, evdev_##func     , ae_flag_none))                                 \
  GWI_B(gwi_func_ini(gwi, "string", #func))\
  GWI_B(gwi_func_arg(gwi, "string", "str"))                   \
  GWI_B(gwi_func_end(gwi, evdev_set_##func , ae_flag_none))
describe_var(name)
describe_var(phys)
describe_var(uniq)

static MFUN(evdev_version) {
  const EvdevInfo* info = INFO(o);
  *(m_int*)RETURN  = libevdev_get_driver_version(info->evdev);
}

static MFUN(evdev_get_num_slot) {
  const EvdevInfo* info = INFO(o);
  *(m_int*)RETURN  = libevdev_get_num_slots(info->evdev);
}

static MFUN(evdev_get_current_slot) {
  const EvdevInfo* info = INFO(o);
  *(m_int*)RETURN  = libevdev_get_current_slot(info->evdev);
}

static MFUN(evdev_grab) {
  const EvdevInfo* info = INFO(o);
  const m_int state = *(m_int*)MEM(SZ_INT);
  *(m_int*)RETURN  = libevdev_grab(info->evdev,
    state ? LIBEVDEV_GRAB : LIBEVDEV_UNGRAB);
}

static MFUN(evdev_led) {
  const EvdevInfo* info = INFO(o);
  const m_int code = *(m_int*)MEM(SZ_INT);
  const m_int state = *(m_int*)MEM(SZ_INT*2);
  *(m_int*)RETURN  = libevdev_kernel_set_led_value(info->evdev, code,
    state ? LIBEVDEV_LED_ON : LIBEVDEV_LED_OFF);
}

static MFUN(evdev_clock) {
  const EvdevInfo* info = INFO(o);
  const m_int clock = *(m_int*)MEM(SZ_INT);
  *(m_int*)RETURN  = libevdev_set_clock_id(info->evdev, clock);
}

#define describe_get_set_fetch(func)                                              \
static MFUN(evdev_get_##func##_value) {                                           \
  const EvdevInfo* info = INFO(o);                                                \
  const m_int type = *(m_int*)MEM(SZ_INT);                                        \
  const m_int code = *(m_int*)MEM(SZ_INT*2);                                      \
  *(m_int*)RETURN  = libevdev_get_##func##_value(info->evdev, type, code);        \
}                                                                                 \
static MFUN(evdev_set_##func##_value) {                                           \
  const EvdevInfo* info = INFO(o);                                                \
  const m_int type  = *(m_int*)MEM(SZ_INT);                                       \
  const m_int code  = *(m_int*)MEM(SZ_INT*2);                                     \
  const m_int value = *(m_int*)MEM(SZ_INT*3);                                     \
  *(m_int*)RETURN  = libevdev_set_##func##_value(info->evdev, type, code, value); \
}                                                                                 \
static MFUN(evdev_fetch_##func##_value) {                                         \
  const EvdevInfo* info = INFO(o);                                                \
  const m_int type  = *(m_int*)MEM(SZ_INT);                                       \
  const m_int code  = *(m_int*)MEM(SZ_INT*2);                                     \
  int value = -1;                                                                 \
  libevdev_fetch_##func##_value(info->evdev, type, code, &value);                 \
  *(m_int*)RETURN  = value;                                                       \
}
describe_get_set_fetch(event)
describe_get_set_fetch(slot)
#define import_get_set_fetch(func)                                              \
  GWI_B(gwi_func_ini(gwi, "void", "get_" #func))\
  GWI_B(gwi_func_arg(gwi, "int", "type"))                                    \
  GWI_B(gwi_func_arg(gwi, "int", "code"))                                    \
  GWI_B(gwi_func_end(gwi, evdev_get_##func##_value   , ae_flag_none))                                                \
  GWI_B(gwi_func_ini(gwi, "void", "set_" #func))\
  GWI_B(gwi_func_arg(gwi, "int", "type"))                                    \
  GWI_B(gwi_func_arg(gwi, "int", "code"))                                    \
  GWI_B(gwi_func_arg(gwi, "int", "value"))                                   \
  GWI_B(gwi_func_end(gwi, evdev_set_##func##_value   , ae_flag_none))                                                \
  GWI_B(gwi_func_ini(gwi, "void", "fetch_" #func))\
  GWI_B(gwi_func_arg(gwi, "int", "type"))                                    \
  GWI_B(gwi_func_arg(gwi, "int", "code"))                                    \
  GWI_B(gwi_func_end(gwi, evdev_fetch_##func##_value , ae_flag_none))                                                \

static MFUN(evdev_has_property) {
  const EvdevInfo* info = INFO(o);
  const m_int i = *(m_int*)MEM(SZ_INT);
  *(m_int*)RETURN  = libevdev_has_property(info->evdev, i);
}
static MFUN(evdev_enable_property) {
  const EvdevInfo* info = INFO(o);
  const m_int i = *(m_int*)MEM(SZ_INT);
  *(m_int*)RETURN  = libevdev_enable_property(info->evdev, i);
}

static MFUN(evdev_disable_property) {
  const EvdevInfo* info = INFO(o);
  const m_int i = *(m_int*)MEM(SZ_INT);
  *(m_int*)RETURN  = libevdev_disable_property(info->evdev, i);
}

static MFUN(evdev_has_event_type) {
  const EvdevInfo* info = INFO(o);
  const m_int i = *(m_int*)MEM(SZ_INT);
  *(m_int*)RETURN  = libevdev_has_event_type(info->evdev, i);
}
static MFUN(evdev_enable_event_type) {
  const EvdevInfo* info = INFO(o);
  const m_int i = *(m_int*)MEM(SZ_INT);
  *(m_int*)RETURN  = libevdev_enable_event_type(info->evdev, i);
}
static MFUN(evdev_disable_event_type) {
  const EvdevInfo* info = INFO(o);
  const m_int i = *(m_int*)MEM(SZ_INT);
  *(m_int*)RETURN  = libevdev_disable_event_type(info->evdev, i);
}
static MFUN(evdev_has_event_code) {
  const EvdevInfo* info = INFO(o);
  const m_int i = *(m_int*)MEM(SZ_INT);
  const m_int j = *(m_int*)MEM(SZ_INT*2);
  *(m_int*)RETURN  = libevdev_has_event_code(info->evdev, i, j);
}
static MFUN(evdev_enable_event_code) {
  const EvdevInfo* info = INFO(o);
  const m_int i = *(m_int*)MEM(SZ_INT);
  const m_int j = *(m_int*)MEM(SZ_INT*2);
  const M_Object k = *(M_Object*)MEM(SZ_INT*3);
  *(m_int*)RETURN  = libevdev_enable_event_code(info->evdev, i, j, k);
}
static MFUN(evdev_disable_event_code) {
  const EvdevInfo* info = INFO(o);
  const m_int i = *(m_int*)MEM(SZ_INT);
  const m_int j = *(m_int*)MEM(SZ_INT*2);
  *(m_int*)RETURN  = libevdev_disable_event_code(info->evdev, i, j);
}

#define describe_abs(name)                         \
static MFUN(evdev_get_abs_##name) {                \
  const EvdevInfo* info = INFO(o);                 \
  const m_int code = *(m_int*)MEM(SZ_INT);         \
  libevdev_get_abs_##name(info->evdev, code);      \
}                                                  \
static MFUN(evdev_set_abs_##name) {                \
  const EvdevInfo* info = INFO(o);                 \
  const m_int code = *(m_int*)MEM(SZ_INT);         \
  const m_int val = *(m_int*)MEM(SZ_INT*2);        \
  libevdev_set_abs_##name(info->evdev, code, val); \
}
describe_abs(minimum)
describe_abs(maximum)
describe_abs(fuzz)
describe_abs(flat)
describe_abs(resolution)

static MFUN(evdev_get_abs_info) {
  const EvdevInfo* info = INFO(o);
  const VM_Code code = *(VM_Code*)REG(SZ_INT*2);
  const M_Object obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = obj;
  const struct input_absinfo* abs = libevdev_get_abs_info(info->evdev, *(m_int*)MEM(SZ_INT));
  if(abs) ABSINFO(obj) = (struct input_absinfo*)abs;
  else xfun_handle(shred, "InvalidEvdevInfoRequest");
}

#define import_abs(name)                                                  \
  GWI_B(gwi_func_ini(gwi, "void", "abs_" #name))\
  GWI_B(gwi_func_arg(gwi, "int", "code"))                              \
  GWI_B(gwi_func_end(gwi, evdev_get_abs_##name , ae_flag_none))                                          \
  GWI_B(gwi_func_ini(gwi, "void", "abs_" #name))\
  GWI_B(gwi_func_arg(gwi, "int", "code"))                              \
  GWI_B(gwi_func_arg(gwi, "int", "val"))                               \
  GWI_B(gwi_func_end(gwi, evdev_set_abs_##name , ae_flag_none))                                          \

static MFUN(evdev_set_abs_info) {
  const EvdevInfo* info = INFO(o);
  const m_int code = *(m_int*)MEM(SZ_INT);
  const M_Object obj = *(M_Object*)MEM(SZ_INT*2);
  const struct input_absinfo* abs = ABSINFO(obj);
  libevdev_set_abs_info(info->evdev, code, abs);
}
static MFUN(evdev_kernel_set_abs_info) {
  const EvdevInfo* info = INFO(o);
  const m_int code = *(m_int*)MEM(SZ_INT);
  const M_Object obj = *(M_Object*)MEM(SZ_INT*2);
  const struct input_absinfo* abs = ABSINFO(obj);
  *(m_int*)RETURN = libevdev_kernel_set_abs_info(info->evdev, code, abs);
}
#define import_set_absinfo(type, func)                                          \
  GWI_B(gwi_func_ini(gwi, #type, #func "absinfo"))\
  GWI_B(gwi_func_arg(gwi, "int", "code"))                                    \
  GWI_B(gwi_func_arg(gwi, "AbsInfo", "abs"))                                 \
  GWI_B(gwi_func_end(gwi, evdev_##func##set_abs_info , ae_flag_none))                                                \

#define describe_id(name)                                \
static MFUN(evdev_get_id_##name) {                       \
  const EvdevInfo* info = INFO(o);                       \
  *(m_int*)RETURN = libevdev_get_id_##name(info->evdev); \
}                                                        \
static MFUN(evdev_set_id_##name) {                       \
  const EvdevInfo* info = INFO(o);                       \
  const m_int id = *(m_int*)MEM(SZ_INT);                 \
  libevdev_set_id_##name(info->evdev, id);               \
  *(m_int*)RETURN = id;                                  \
}
describe_id(product)
describe_id(vendor)
describe_id(bustype)
describe_id(version)
#define import_id(name)                                         \
  GWI_B(gwi_func_ini(gwi, "int", #name))\
  GWI_B(gwi_func_end(gwi, evdev_get_id_##name , ae_flag_none))                                 \
  GWI_B(gwi_func_ini(gwi, "int", #name))\
  GWI_B(gwi_func_arg(gwi, "int", "id"))                       \
  GWI_B(gwi_func_end(gwi, evdev_set_id_##name , ae_flag_none))                                 \

#define describe_from_name(func)                                \
static MFUN(evdev_##func##_from_name) {                       \
  const M_Object str = *(M_Object*)MEM(SZ_INT);                 \
  *(m_int*)RETURN = libevdev_##func##_from_name(STRING(str)); \
}                                                        \
static MFUN(evdev_##func##_from_name_n) {                       \
  const M_Object str = *(M_Object*)MEM(SZ_INT);                 \
  const m_int n  = *(m_int*)MEM(SZ_INT*2);                 \
  *(m_int*)RETURN = libevdev_##func##_from_name_n(STRING(str), n); \
}
describe_from_name(event_type)
describe_from_name(property)
#define import_from_name(func)                                         \
  GWI_B(gwi_func_ini(gwi, "int", #func))\
  GWI_B(gwi_func_arg(gwi, "string", "name"))                       \
  GWI_B(gwi_func_end(gwi, evdev_##func##_from_name , ae_flag_static))                                 \
  GWI_B(gwi_func_ini(gwi, "int", #func))\
  GWI_B(gwi_func_arg(gwi, "string", "name"))                       \
  GWI_B(gwi_func_arg(gwi, "int", "n"))                       \
  GWI_B(gwi_func_end(gwi, evdev_##func##_from_name_n , ae_flag_static))                                 \

static MFUN(evdev_event_code_from_name) {
  const m_int type = *(m_int*)MEM(SZ_INT);
  const M_Object str = *(M_Object*)MEM(SZ_INT*2);
  *(m_int*)RETURN = libevdev_event_code_from_name(type, STRING(str));
}

static MFUN(evdev_eventcode_from_name_n) {
  const m_int type = *(m_int*)MEM(SZ_INT);
  const M_Object str = *(M_Object*)MEM(SZ_INT);
  const m_int n  = *(m_int*)MEM(SZ_INT*2);
  *(m_int*)RETURN = libevdev_event_code_from_name_n(type, STRING(str), n);
}

static MFUN(uinput_create) {
  const EvdevInfo* info = INFO(o);
  const int uifd = open("/dev/uinput", O_RDWR);
  const VM_Code code = *(VM_Code*)REG(SZ_INT);
  const M_Object uinput = *(M_Object*)RETURN = new_object(shred->info->mp, code->ret_type);
  if(libevdev_uinput_create_from_device(info->evdev, uifd, &UINPUT(uinput)))
    xfun_handle(shred, "UinputCreationError");
}

GWION_IMPORT(Evdev) {

  DECL_B(const Type, t_evdev, = gwi_class_ini(gwi, "Evdev", "Event"));
  gwi_class_xtor(gwi, NULL, evdev_dtor);
  t_evdev->nspc->offset += sizeof(EvdevInfo);

  GWI_B(gwimport_absinfo(gwi));
  GWI_B(gwimport_evdevev(gwi));

  GWI_B(gwi_func_ini(gwi, "auto", "new"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_end(gwi, evdev_index, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "get_repeat"))
  GWI_B(gwi_func_arg(gwi, "&int", "delay"))
  GWI_B(gwi_func_arg(gwi, "&int", "period"))
  GWI_B(gwi_func_end(gwi, evdev_get_repeat, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "index"))
  GWI_B(gwi_func_end(gwi, evdev_get_index, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "num_slots"))
  GWI_B(gwi_func_end(gwi, evdev_get_num_slot, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "curent_slot"))
  GWI_B(gwi_func_end(gwi, evdev_get_current_slot, ae_flag_none))

  import_var(name)
  import_var(phys)
  import_var(uniq)

  GWI_B(gwi_func_ini(gwi, "int", "recv"))
  GWI_B(gwi_func_arg(gwi, "Ev", "ev"))
  GWI_B(gwi_func_end(gwi, evdev_recv, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "version"))
  GWI_B(gwi_func_end(gwi, evdev_version, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "grab"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_end(gwi, evdev_grab, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "clock"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_end(gwi, evdev_clock, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "led"))
  GWI_B(gwi_func_arg(gwi, "int", "code"))
  GWI_B(gwi_func_arg(gwi, "int", "state"))
  GWI_B(gwi_func_end(gwi, evdev_led, ae_flag_none))

  import_abs(minimum)
  import_abs(maximum)
  import_abs(fuzz)
  import_abs(flat)
  import_abs(resolution)
  import_set_absinfo(void,)
  import_set_absinfo(int, kernel_)
  GWI_B(gwi_func_ini(gwi, "AbsInfo", "get_absinfo"))
  GWI_B(gwi_func_arg(gwi, "int", "code"))
  GWI_B(gwi_func_end(gwi, evdev_get_abs_info, ae_flag_none))

  import_id(product)
  import_id(vendor)
  import_id(bustype)
  import_id(version)

  import_from_name(event_type)
  import_from_name(property)
  GWI_B(gwi_func_ini(gwi, "int", "event_code"))
  GWI_B(gwi_func_arg(gwi, "int", "type"))
  GWI_B(gwi_func_arg(gwi, "string", "name"))
  GWI_B(gwi_func_end(gwi, evdev_event_code_from_name, ae_flag_static))
  GWI_B(gwi_func_ini(gwi, "int", "event_code"))
  GWI_B(gwi_func_arg(gwi, "string", "name"))
  GWI_B(gwi_func_arg(gwi, "int", "n"))
  GWI_B(gwi_func_end(gwi, evdev_eventcode_from_name_n, ae_flag_static))

  GWI_B(gwi_func_ini(gwi, "int", "has_property"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_end(gwi, evdev_has_property, ae_flag_none))
  GWI_B(gwi_func_ini(gwi, "int", "enable_property"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_end(gwi, evdev_enable_property, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "disable_property"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_end(gwi, evdev_disable_property, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "has_event_type"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_end(gwi, evdev_has_event_type, ae_flag_none))
  GWI_B(gwi_func_ini(gwi, "int", "enable_event_type"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_end(gwi, evdev_enable_event_type, ae_flag_none))
  GWI_B(gwi_func_ini(gwi, "int", "disable_event_type"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_end(gwi, evdev_disable_event_type, ae_flag_none))
  GWI_B(gwi_func_ini(gwi, "int", "has_event_code"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_arg(gwi, "int", "code"))
  GWI_B(gwi_func_end(gwi, evdev_has_event_code, ae_flag_none))
  GWI_B(gwi_func_ini(gwi, "int", "enable_event_code"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_arg(gwi, "int", "code"))
  GWI_B(gwi_func_arg(gwi, "Object", "opt"))
  GWI_B(gwi_func_end(gwi, evdev_enable_event_code, ae_flag_none))
  GWI_B(gwi_func_ini(gwi, "int", "disable_event_code"))
  GWI_B(gwi_func_arg(gwi, "int", "i"))
  GWI_B(gwi_func_arg(gwi, "int", "code"))
  GWI_B(gwi_func_end(gwi, evdev_disable_event_code, ae_flag_none))

  import_get_set_fetch(event)
  import_get_set_fetch(slot)

  GWI_B(gwimport_uinput(gwi));
  GWI_B(gwi_func_ini(gwi, "Uinput", "uinput"))
  GWI_B(gwi_func_end(gwi, uinput_create, ae_flag_none))
  GWI_B(gwi_class_end(gwi))

  return true;
}
