#ifndef __GWEVDEV
#define __GWEVDEV

typedef struct input_event InputEvent;
MK_VECTOR_TYPE(InputEvent, inputevent)

typedef struct {
  struct libevdev* evdev;
  pthread_t thread;
  pthread_mutex_t mutex;
  InputEventList *args;
  MemPool mp;
  uint32_t idx;
  int index;
  int fd;
} EvdevInfo;

GWION_IMPORT(absinfo);
#define ABSINFO(o) *(struct input_absinfo**)(o->data)

GWION_IMPORT(uinput);
#define UINPUT(o) *(struct libevdev_uinput**)(o->data)

GWION_IMPORT(evdevev);
typedef struct gw_input_event {
  m_int type;
  m_int code;
  m_int value;
  m_int sec;
  m_int usec;
} gw_input_event;
#define EVDEVEV(o) (gw_input_event*)((o)->data)
#endif
