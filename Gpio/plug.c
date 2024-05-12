//#include  <unistd.h>
#include "gpiod.h"
#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "instr.h"
#include "gwion.h"
#include "object.h"
#include "operator.h"
#include "import.h"
#include "gwi.h"
#include "shreduler_private.h"

#define CHIP(o) (*(struct gpiod_chip **)(o)->data)
#define LINE(o) (*(struct gpiod_line **)(o)->data)
#define BULK(o) ((struct gpiod_line_bulk *)(o)->data)

static MFUN(gw_gpiod_chip_new) {
  const M_Object path = *(M_Object*)MEM(SZ_INT);
  *(M_Object*)RETURN = o;
  if(!(CHIP(o) = gpiod_chip_open_lookup(STRING(path))))
    xfun_handle(shred, "GpioChip");
}

static SFUN(gw_gpiod_chip_open_by_path) {
  const M_Object path = *(M_Object*)MEM(0);
  const VM_Code code = *(VM_Code*)REG(SZ_INT);
  const M_Object obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = obj;
  if(!(CHIP(obj) = gpiod_chip_open(STRING(path))))
    xfun_handle(shred, "GpioChip");
}

static SFUN(gw_gpiod_chip_open_by_name) {
  const M_Object name = *(M_Object*)MEM(0);
  const VM_Code code = *(VM_Code*)REG(SZ_INT);
  const M_Object obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = obj;
  if(!(CHIP(obj) = gpiod_chip_open_by_name(STRING(name))))
    xfun_handle(shred, "GpioChip");
}

static SFUN(gw_gpiod_chip_open_by_number) {
  const m_int num = *(m_int*)MEM(0);
  const VM_Code code = *(VM_Code*)REG(SZ_INT);
  const M_Object obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = obj;
  if(!(CHIP(obj) = gpiod_chip_open_by_number(num)))
    xfun_handle(shred, "GpioChip");
}

static SFUN(gw_gpiod_chip_open_by_label) {
  const M_Object label = *(M_Object*)MEM(0);
  const VM_Code code = *(VM_Code*)REG(SZ_INT);
  const M_Object obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = obj;
  if(!(CHIP(obj) = gpiod_chip_open_by_label(STRING(label))))
    xfun_handle(shred, "GpioChip");
}

static MFUN(gw_gpiod_dtor) {
  gpiod_chip_close(CHIP(o));
}

static MFUN(gw_gpiod_chip_name) {
  const char *result = gpiod_chip_name(CHIP(o));
  *(M_Object*)RETURN = new_string(shred->info->vm->gwion, (m_str)result);
}

static MFUN(gw_gpiod_chip_label) {
  char const * result = gpiod_chip_label(CHIP(o));
  *(M_Object*)RETURN = new_string(shred->info->vm->gwion, (m_str)result);
}

static MFUN(gw_gpiod_chip_num_lines) {
  *(m_int*)RETURN = gpiod_chip_num_lines(CHIP(o));
}

static MFUN(gw_gpiod_chip_get_line) {
  unsigned int offset = (unsigned int)*(m_int*)MEM(SZ_INT);
  const VM_Code code = *(VM_Code*)REG(SZ_INT*2);
  M_Object ret_obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = ret_obj;
  if(!(LINE(ret_obj) = gpiod_chip_get_line(CHIP(o), offset)))
     xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_chip_get_lines) {
  const M_Object temp2 = *(M_Object*)MEM(SZ_INT);
  M_Vector arg2 = ARRAY(temp2);
  const VM_Code code = *(VM_Code*)REG(SZ_INT*2);
  M_Object ret_obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = ret_obj;
  if(gpiod_chip_get_lines(CHIP(o), (unsigned int*)ARRAY_PTR(arg2), ARRAY_LEN(arg2), BULK(ret_obj)) == -1)
    xfun_handle(shred, "GpioBulk");
}

static MFUN(gw_gpiod_chip_get_all_lines) {
  const VM_Code code = *(VM_Code*)REG(SZ_INT);
  M_Object ret_obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = ret_obj;
  if(gpiod_chip_get_all_lines(CHIP(o), BULK(ret_obj)) == -1)
    xfun_handle(shred, "GpioBulk");
}

static MFUN(gw_gpiod_chip_find_line) {
  const VM_Code code = *(VM_Code*)REG(SZ_INT);
  M_Object name = *(M_Object*)MEM(SZ_INT);
  M_Object ret_obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = ret_obj;
  LINE(ret_obj) = gpiod_chip_find_line(CHIP(o), STRING(name));
  if(!LINE(ret_obj)) xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_chip_find_lines) {
  const VM_Code code = *(VM_Code*)REG(SZ_INT);
  M_Object names = *(M_Object*)MEM(SZ_INT);
  M_Vector array = ARRAY(names);
  const char *tmp[ARRAY_LEN(array)];
  for(m_uint i = 0; i < ARRAY_LEN(array); i++) {
    const M_Object name = *(M_Object*)ARRAY_PTR(array) + i * SZ_INT;
    tmp[i] = STRING(name);
  }
  M_Object ret_obj = new_object(shred->info->mp, code->ret_type);
  *(M_Object*)RETURN = ret_obj;
  if(gpiod_chip_find_lines(CHIP(o), tmp, BULK(ret_obj)) == -1)
    xfun_handle(shred, "GpioLine");
}


static MFUN(gw_gpiod_line_bulk_reset) {
  gpiod_line_bulk_init(BULK(o));
}

static MFUN(gw_gpiod_line_bulk_add_line) {
  const M_Object temp2 = *(M_Object*)MEM(SZ_INT);
  struct gpiod_line * arg2 = LINE(temp2);
  gpiod_line_bulk_add(BULK(o), arg2);
}

static MFUN(gw_gpiod_line_bulk_get_line) {
  unsigned int arg2 = (unsigned int)*(m_int*)MEM(SZ_INT);
  struct gpiod_line * result = gpiod_line_bulk_get_line(BULK(o), arg2);
  const VM_Code code = *(VM_Code*)REG(SZ_INT*2);
  M_Object ret_obj = new_object(shred->info->mp, code->ret_type);
  LINE(ret_obj) = result;
  *(M_Object*)RETURN = ret_obj;
  if(!result) xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_line_bulk_num_lines) {
  *(m_int*)RETURN = gpiod_line_bulk_num_lines(BULK(o));
}
/*
static MFUN(gw_gpiod_line_bulk_foreach_line) {
  const M_Object temp2 = *(M_Object*)MEM(SZ_INT);
  gpiod_line_bulk_foreach_cb arg2 = *(gpiod_line_bulk_foreach_cb*)(temp2->data);
  const M_Object temp3 = *(M_Object*)MEM(SZ_INT*2);
  void * arg3 = *(void **)(temp3->data);
  gpiod_line_bulk_foreach_line(BULK(o),arg2,arg3);
}
*/
static MFUN(gw_gpiod_line_offset) {
  *(m_int*)RETURN = gpiod_line_offset(LINE(o));
}

static MFUN(gw_gpiod_line_name) {
  char const * result = gpiod_line_name(LINE(o));
  *(M_Object*)RETURN = new_string(shred->info->vm->gwion, (m_str)result ?: "");
}

static MFUN(gw_gpiod_line_consumer) {
  char const * result = gpiod_line_consumer(LINE(o));
  *(M_Object*)RETURN = new_string(shred->info->vm->gwion, (m_str)result ?: (m_str)"");
}

static MFUN(gw_gpiod_line_direction) {
  *(m_int*)RETURN = gpiod_line_direction(LINE(o));
}

static MFUN(gw_gpiod_line_state) {
  *(m_int*)RETURN = gpiod_line_active_state(LINE(o));
}

static MFUN(gw_gpiod_line_bias) {
  *(m_int*)RETURN = gpiod_line_bias(LINE(o));
}

static MFUN(gw_gpiod_line_is_used) {
  *(m_int*)RETURN = gpiod_line_is_used(LINE(o));
}

static MFUN(gw_gpiod_line_is_open_drain) {
  *(m_int*)RETURN = gpiod_line_is_open_drain(LINE(o));
}

static MFUN(gw_gpiod_line_is_open_source) {
  *(m_int*)RETURN = gpiod_line_is_open_source(LINE(o));
}

/*
// we'd need a way to read error number
static MFUN(gw_gpiod_line_update) {
  *(m_int*)RETURN = gpiod_line_update(LINE(o));
}
static MFUN(gw_gpiod_line_needs_update) {
  *(m_int*)RETURN = gpiod_line_needs_update(LINE(o));
}
*/

/*
static MFUN(gw_gpiod_line_close_chip) {
  gpiod_line_close_chip(LINE(o));
}
static MFUN(gw_gpiod_line_get_chip) {
  struct gpiod_chip * result = gpiod_line_get_chip(LINE(o));
  const VM_Code code = *(vm_code*)REG(SZ_INT);
  M_Object ret_obj = new_object(shred->info->mp, code->ret_type);
  CHIP(ret_obj) = result;
  *(M_Object*)RETURN = ret_obj;
}
*/

#define mk_line_request(name)                                                     \
static MFUN(gw_gpiod_line_request_##name) {                                       \
  M_Object consumer = *(M_Object*)MEM(SZ_INT);                                    \
  if(gpiod_line_request_##name(LINE(o), STRING(consumer)) < 0)                    \
    xfun_handle(shred, "GpioLine");                                               \
}                                                                                 \
static MFUN(gw_gpiod_line_request_##name##_flags) {                               \
  M_Object temp2 = *(M_Object*)MEM(SZ_INT);                                       \
  int arg3 = (int)*(m_int*)MEM(SZ_INT*2);                                         \
  if(gpiod_line_request_##name##_flags(LINE(o), STRING(temp2),arg3) < 0)          \
    xfun_handle(shred, "GpioLine");                                               \
}                                                                                 \
static MFUN(gw_gpiod_line_request_bulk_##name) {                                  \
  M_Object consumer = *(M_Object*)MEM(SZ_INT);                                    \
  if(gpiod_line_request_bulk_##name(BULK(o), STRING(consumer)) < 0)               \
    xfun_handle(shred, "GpioBulk");                                               \
}                                                                                 \
static MFUN(gw_gpiod_line_request_bulk_##name##_flags) {                          \
  M_Object consumer = *(M_Object*)MEM(SZ_INT);                                    \
  int arg3 = *(m_int*)MEM(SZ_INT*2);                                              \
  if(gpiod_line_request_bulk_##name##_flags(BULK(o), STRING(consumer), arg3) < 0) \
    xfun_handle(shred, "GpioBulk");                                               \
}
mk_line_request(input);
mk_line_request(rising_edge_events);
mk_line_request(falling_edge_events);
mk_line_request(both_edges_events);

static MFUN(gw_gpiod_line_request_output) {
  M_Object temp2 = *(M_Object*)MEM(SZ_INT);
  int arg3 = *(m_int*)MEM(SZ_INT*2);
  if(gpiod_line_request_output(LINE(o), STRING(temp2), arg3) < 0)
    xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_line_request_output_flags) {
  M_Object consumer = *(M_Object*)MEM(SZ_INT);
  int arg3 = (int)*(m_int*)MEM(SZ_INT*2);
  int arg4 = (int)*(m_int*)MEM(SZ_INT*3);
  if(gpiod_line_request_output_flags(LINE(o), STRING(consumer),arg3,arg4) < 0)
    xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_line_release) {
  gpiod_line_release(LINE(o));
}

static MFUN(gw_gpiod_line_is_requested) {
  *(m_int*)RETURN = gpiod_line_is_requested(LINE(o));
}

static MFUN(gw_gpiod_line_is_free) {
  *(m_int*)RETURN = gpiod_line_is_free(LINE(o));
}

static MFUN(gw_gpiod_line_get_value) {
  if((*(m_int*)RETURN = gpiod_line_get_value(LINE(o))) < 0)
    xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_line_set_value) {
  int arg2 = (int)*(m_int*)MEM(SZ_INT);
  if((*(m_int*)RETURN = gpiod_line_set_value(LINE(o), arg2)) < 0)
    xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_line_set_config) {
  int arg2 = (int)*(m_int*)MEM(SZ_INT);
  int arg3 = (int)*(m_int*)MEM(SZ_INT*2);
  int arg4 = (int)*(m_int*)MEM(SZ_INT*3);
  if(gpiod_line_set_config(LINE(o),arg2,arg3,arg4) < 0)
    xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_line_set_flags) {
  int arg2 = *(m_int*)MEM(SZ_INT);
  if(gpiod_line_set_flags(LINE(o),arg2) < 0)
    xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_line_set_direction_input) {
  if(gpiod_line_set_direction_input(LINE(o)) < 0)
    xfun_handle(shred, "GpioLine");
}

static MFUN(gw_gpiod_line_set_direction_output) {
  int arg2 = *(m_int*)MEM(SZ_INT);
  if(gpiod_line_set_direction_output(LINE(o),arg2) < 0)
    xfun_handle(shred, "GpioLine");
}

struct line2nowdata {
  VM_Shred shred;
  struct gpiod_line * line;
  struct timespec ts;
  bool has_ts;
};

static void line2nowrun(void *data) {
  struct line2nowdata *l2nd = data;
  VM_Shred shred = l2nd->shred;
  struct gpiod_line * line = l2nd->line;
  struct timespec ts = l2nd->ts;
  const bool has_ts = l2nd->has_ts;
  mp_free2(shred->info->mp, sizeof(struct line2nowdata), l2nd);
  const int result = gpiod_line_event_wait(line, has_ts ? &ts : NULL);
  if(!has_ts) shred->reg -= SZ_INT;
  if(result == 1)
    shredule(shred->tick->shreduler, shred, GWION_EPSILON);
  else if(result == 0) {
    xfun_handle(shred, "GpioEventTimeOut");
  } else xfun_handle(shred, "GpioEventTimeOut");
}

static INSTR(line2now) {
  POP_REG(shred, SZ_FLOAT);
  const M_Object o = *(M_Object*)REG(-SZ_INT);
  struct line2nowdata *l2nd = mp_malloc2(shred->info->mp, sizeof(struct line2nowdata));
  l2nd->shred = shred;
  l2nd->line = LINE(o);
  l2nd->has_ts = false;
  shred_pool_run(shred, line2nowrun, l2nd);
}

static MFUN(gw_gpiod_line_request_bulk_output) {
  M_Object consumer = *(M_Object*)MEM(SZ_INT);
  const M_Object tmp = *(M_Object*)MEM(SZ_INT*2);
  M_Vector vals = ARRAY(tmp);
  if(gpiod_line_request_bulk_output(BULK(o), STRING(consumer),(int const *)ARRAY_PTR(vals)) < 0)
    xfun_handle(shred, "GpioBulk");
}

static MFUN(gw_gpiod_line_request_bulk_output_flags) {
  M_Object consumer = *(M_Object*)MEM(SZ_INT);
  int flags = *(m_int*)MEM(SZ_INT*2);
  const M_Object tmp = *(M_Object*)MEM(SZ_INT*3);
  M_Vector vals = ARRAY(tmp);
  if(gpiod_line_request_bulk_output_flags(BULK(o), STRING(consumer), flags, (int const *)ARRAY_PTR(vals)) < 0)
    xfun_handle(shred, "GpioBulk");
}

static MFUN(gw_gpiod_line_release_bulk) {
  gpiod_line_release_bulk(BULK(o));
}

static MFUN(gw_gpiod_line_get_value_bulk) {
  const VM_Code code = *(VM_Code*)REG(SZ_INT);
  const unsigned int sz = gpiod_line_bulk_num_lines(BULK(o));
  const M_Object ret_obj = new_array(shred->info->mp, code->ret_type, sz);
  M_Vector vals = ARRAY(ret_obj);
  int result = (int)gpiod_line_get_value_bulk(BULK(o), (int *)ARRAY_PTR(vals));
  *(m_int*)RETURN = (m_int)ret_obj;
  if(result < 0) xfun_handle(shred, "GpioBulk");
}

static MFUN(gw_gpiod_line_set_value_bulk) {
  const M_Object tmp = *(M_Object*)MEM(SZ_INT);
  M_Vector vals = ARRAY(tmp);
  if(ARRAY_LEN(vals) < gpiod_line_bulk_num_lines(BULK(o)))
    xfun_handle(shred, "GpioBulkArrayTooSmall");
  if(gpiod_line_set_value_bulk(BULK(o),(int const *)ARRAY_PTR(vals)) < 0)
    xfun_handle(shred, "GpioBulk");
}

static MFUN(gw_gpiod_line_set_config_bulk) {
  int arg2 = (int)*(m_int*)MEM(SZ_INT);
  int arg3 = (int)*(m_int*)MEM(SZ_INT*2);
  const M_Object tmp = *(M_Object*)MEM(SZ_INT*3);
  M_Vector vals = ARRAY(tmp);
  if(ARRAY_LEN(vals) < gpiod_line_bulk_num_lines(BULK(o)))
    xfun_handle(shred, "GpioBulkArrayTooSmall");
  if(gpiod_line_set_config_bulk(BULK(o),arg2,arg3,(int const *)ARRAY_PTR(vals)) < 0)
    xfun_handle(shred, "GpioBulk");
}

static MFUN(gw_gpiod_line_set_flags_bulk) {
  int arg2 = (int)*(m_int*)MEM(SZ_INT);
  if(gpiod_line_set_flags_bulk(BULK(o), arg2) < 0)
    xfun_handle(shred, "GpioBulk");
}

static MFUN(gw_gpiod_line_set_direction_input_bulk) {
  if(gpiod_line_set_direction_input_bulk(BULK(o)) < 0)
    xfun_handle(shred, "GpioBulk");
}

static MFUN(gw_gpiod_line_set_direction_output_bulk) {
  const M_Object tmp = *(M_Object*)MEM(SZ_INT);
  M_Vector vals = ARRAY(tmp);
  if(ARRAY_LEN(vals) < gpiod_line_bulk_num_lines(BULK(o)))
    xfun_handle(shred, "GpioBulkArrayToSmall");
  if(gpiod_line_set_direction_output_bulk(BULK(o), (const int *)ARRAY_PTR(vals)) < 0)
    xfun_handle(shred, "GpioBulk");
}

#define LINEEV(o) ((struct gpiod_line_event*)((o)->data))

#define NANO 1000000
ANN static inline m_float ts2dur(const VM_Shred shred, struct timespec *ts) {
  const uint64_t nsum = (ts->tv_sec * NANO) + ts->tv_nsec;
  const uint64_t nrate = shred->tick->shreduler->bbq->si->sr * NANO;
  return (m_float)nsum / (m_float)nrate;
}

static void dur2ts(const VM_Shred shred, const m_float dur, struct timespec *ts) {
  const m_float ndur = (dur * NANO) / shred->tick->shreduler->bbq->si->sr;
  ts->tv_sec = ndur / NANO;
  ts->tv_nsec = (m_uint)ndur % NANO;
}
#undef NANO

static MFUN(gw_gpiod_line_event_ts_get) {
  *(m_int*)RETURN = ts2dur(shred, &LINEEV(o)->ts);
}

static MFUN(gw_gpiod_line_event_event_type_get) {
  *(m_int*)RETURN = LINEEV(o)->event_type;
}

static MFUN(gw_gpiod_line_event_wait) {
  const m_float dur = *(m_float*)MEM(SZ_INT);
  struct timespec ts;
  dur2ts(shred, dur, &ts);
  struct line2nowdata *l2nd = mp_malloc2(shred->info->mp, sizeof(struct line2nowdata));
  l2nd->shred = shred;
  l2nd->line = LINE(o);
  l2nd->ts = ts;
  l2nd->has_ts = true;
  shred_pool_run(shred, line2nowrun, l2nd);
}

struct bulk2nowdata {
  VM_Shred shred;
  struct gpiod_line_bulk * bulk;
  struct timespec ts;
//  struct gpiod_line_bulk *out;
  bool has_ts;
};

static void bulk2nowrun(void *data) {
  struct bulk2nowdata *b2nd = data;
  VM_Shred shred = b2nd->shred;
  struct gpiod_line_bulk * bulk = b2nd->bulk;
  struct timespec ts = b2nd->ts;
  const bool has_ts = b2nd->has_ts;
  mp_free2(shred->info->mp, sizeof(struct bulk2nowdata), b2nd);
  const int result = gpiod_line_event_wait_bulk(bulk, has_ts ? &ts : NULL, NULL);
  if(!has_ts) shred->reg -= SZ_INT;
  if(result == 1)
    shredule(shred->tick->shreduler, shred, GWION_EPSILON);
  else if(result == 0) {
    xfun_handle(shred, "GpioEventTimeOut");
  } else xfun_handle(shred, "GpioEventTimeOut");
}

static INSTR(bulk2now) {
  POP_REG(shred, SZ_FLOAT);
  const M_Object o = *(M_Object*)REG(-SZ_INT);
  struct bulk2nowdata *b2nd = mp_malloc2(shred->info->mp, sizeof(struct bulk2nowdata));
  b2nd->shred = shred;
  b2nd->bulk = BULK(o);
  b2nd->has_ts = false;
  //b2nd->out = NULL;
  shred_pool_run(shred, bulk2nowrun, b2nd);
}

static MFUN(gw_gpiod_line_event_read) {
  const VM_Code code = *(VM_Code*)REG(SZ_INT*2);
  M_Object ret_obj = new_object(shred->info->mp, code->ret_type);
  struct gpiod_line_event le;
  int result = (int)gpiod_line_event_read(LINE(o), &le);
  *LINEEV(ret_obj) = le;
  *(M_Object*)RETURN = ret_obj;
  if(result < 0) xfun_handle(shred, "GpioEvent");
}

static MFUN(gw_gpiod_line_event_read_multiple) {
  unsigned int num_ev = (unsigned int)*(m_int*)MEM(SZ_INT);
  const VM_Code code = *(VM_Code*)REG(SZ_INT*2);
  M_Object ret_obj = new_array(shred->info->mp, code->ret_type, num_ev);
  M_Vector events = ARRAY(ret_obj);
  *(M_Object*)RETURN = ret_obj;
  struct gpiod_line_event * array = mp_malloc2(shred->info->mp, sizeof(struct gpiod_line_event) * num_ev);
  int result = (int)gpiod_line_event_read_multiple(LINE(o), array, num_ev);
  const Type t = array_base(code->ret_type);
  for(unsigned int i = 0; i < num_ev; i++) {
    M_Object obj = new_object(shred->info->mp, t);
    *LINEEV(obj) = array[i];
    *(M_Object*)(ARRAY_PTR(events) + i * SZ_INT) = obj;
  }
  mp_free2(shred->info->mp, sizeof(struct gpiod_line_event) * num_ev, array);
  if(result < 0) xfun_handle(shred, "GpioEvent");
}

static MFUN(gw_gpiod_line_event_wait_bulk) {
  const m_float dur = *(m_float*)MEM(SZ_INT);
  struct timespec ts;
  dur2ts(shred, dur, &ts);
  struct bulk2nowdata *b2nd = mp_malloc2(shred->info->mp, sizeof(struct bulk2nowdata));
  b2nd->shred = shred;
  b2nd->bulk = BULK(o);
  b2nd->ts = ts;
  //b2nd->out = NULL;
  b2nd->has_ts = true;
  shred_pool_run(shred, bulk2nowrun, b2nd);
}
/*
static MFUN(gw_gpiod_line_event_wait_bulk2) {
  const m_float dur = *(m_float*)MEM(SZ_INT);
  const M_Object out = *(M_Object*)MEM(SZ_INT + SZ_FLOAT);
  struct timespec ts;
  dur2ts(shred, dur, &ts);
  struct bulk2nowdata *b2nd = mp_malloc2(shred->info->mp, sizeof(struct bulk2nowdata));
  b2nd->shred = shred;
  b2nd->bulk = BULK(o);
  b2nd->ts = ts;
  //b2nd->out = BULK(out);
  b2nd->has_ts = true;
  shred_pool_run(shred, bulk2nowrun, l2nd);
}
*/
static MFUN(gw_gpiod_version_string) {
  char const * result = gpiod_version_string();
  *(M_Object*)RETURN = new_string(shred->info->vm->gwion, (m_str)result);
}

GWION_IMPORT(Gpio) {
  DECL_B(const Type, t_gpio, = gwi_class_ini(gwi, "Gpio", "Object"));
    t_gpio->nspc->offset += SZ_INT;
    gwi_class_xtor(gwi, NULL, gw_gpiod_dtor);

    gwidoc(gwi, "Get the API version of the library as a human-readable string.  ");
    GWI_B(gwi_func_ini(gwi, "string", "version"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_version_string, ae_flag_static));
/*
    GWI_B(gwi_enum_ini(gwi, (m_str)"CB"));
    GWI_B(gwi_enum_add(gwi, (m_str)"NEXT", (m_uint)GPIOD_LINE_BULK_CB_NEXT));
    GWI_B(gwi_enum_add(gwi, (m_str)"STOP", (m_uint)GPIOD_LINE_BULK_CB_STOP));
    GWI_B(gwi_enum_end(gwi));
*/
    GWI_B(gwi_enum_ini(gwi, (m_str)"Direction"));
    GWI_B(gwi_enum_add(gwi, (m_str)"INPUT", (m_uint)GPIOD_LINE_DIRECTION_INPUT));
    GWI_B(gwi_enum_add(gwi, (m_str)"OUTPUT", (m_uint)GPIOD_LINE_DIRECTION_OUTPUT));
    GWI_B(gwi_enum_end(gwi));

    GWI_B(gwi_enum_ini(gwi, (m_str)"State"));
    GWI_B(gwi_enum_add(gwi, (m_str)"HIGH", (m_uint)GPIOD_LINE_ACTIVE_STATE_HIGH));
    GWI_B(gwi_enum_add(gwi, (m_str)"LOW", (m_uint)GPIOD_LINE_ACTIVE_STATE_LOW));
    GWI_B(gwi_enum_end(gwi));
/*
    GWI_B(gwi_enum_ini(gwi, (m_str)"Drive"));
    GWI_B(gwi_enum_add(gwi, (m_str)"PUSH_PULL", (m_uint)GPIOD_LINE_DRIVE_PUSH_PULL));
    GWI_B(gwi_enum_add(gwi, (m_str)"OPEN_DRAIN", (m_uint)GPIOD_LINE_DRIVE_OPEN_DRAIN));
    GWI_B(gwi_enum_add(gwi, (m_str)"OPEN_SOURCE", (m_uint)GPIOD_LINE_DRIVE_OPEN_SOURCE));
    GWI_B(gwi_enum_end(gwi));
*/
    GWI_B(gwi_enum_ini(gwi, (m_str)"Bias"));
    GWI_B(gwi_enum_add(gwi, (m_str)"AS_IS", (m_uint)GPIOD_LINE_BIAS_AS_IS));
    GWI_B(gwi_enum_add(gwi, (m_str)"DISABLE", (m_uint)GPIOD_LINE_BIAS_DISABLE));
    GWI_B(gwi_enum_add(gwi, (m_str)"PULL_UP", (m_uint)GPIOD_LINE_BIAS_PULL_UP));
    GWI_B(gwi_enum_add(gwi, (m_str)"PULL_DOWN", (m_uint)GPIOD_LINE_BIAS_PULL_DOWN));
    GWI_B(gwi_enum_end(gwi));

    GWI_B(gwi_enum_ini(gwi, (m_str)"Request"));
    GWI_B(gwi_enum_add(gwi, (m_str)"DIRECTION_AS_IS", (m_uint)GPIOD_LINE_REQUEST_DIRECTION_AS_IS));
    GWI_B(gwi_enum_add(gwi, (m_str)"DIRECTION_INPUT", (m_uint)GPIOD_LINE_REQUEST_DIRECTION_INPUT));
    GWI_B(gwi_enum_add(gwi, (m_str)"DIRECTION_OUTPUT", (m_uint)GPIOD_LINE_REQUEST_DIRECTION_OUTPUT));
    GWI_B(gwi_enum_add(gwi, (m_str)"EVENT_FALLING_EDGE", (m_uint)GPIOD_LINE_REQUEST_EVENT_FALLING_EDGE));
    GWI_B(gwi_enum_add(gwi, (m_str)"EVENT_RISING_EDGE", (m_uint)GPIOD_LINE_REQUEST_EVENT_RISING_EDGE));
    GWI_B(gwi_enum_add(gwi, (m_str)"EVENT_BOTH_EDGES", (m_uint)GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES));
    GWI_B(gwi_enum_end(gwi));

    GWI_B(gwi_enum_ini(gwi, (m_str)"Flag"));
    GWI_B(gwi_enum_add(gwi, (m_str)"OPEN_DRAIN", (m_uint)GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN));
    GWI_B(gwi_enum_add(gwi, (m_str)"OPEN_SOURCE", (m_uint)GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE));
    GWI_B(gwi_enum_add(gwi, (m_str)"ACTIVE_LOW", (m_uint)GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW));
    GWI_B(gwi_enum_add(gwi, (m_str)"BIAS_DISABLE", (m_uint)GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE));
    GWI_B(gwi_enum_add(gwi, (m_str)"BIAS_PULL_DOWN", (m_uint)GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN));
    GWI_B(gwi_enum_add(gwi, (m_str)"BIAS_PULL_UP", (m_uint)GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP));
    GWI_B(gwi_enum_end(gwi));

    GWI_B(gwi_enum_ini(gwi, (m_str)"EvType"));
    GWI_B(gwi_enum_add(gwi, (m_str)"RISING_EDGE", (m_uint)GPIOD_LINE_EVENT_RISING_EDGE));
    GWI_B(gwi_enum_add(gwi, (m_str)"FALLING_EDGE", (m_uint)GPIOD_LINE_EVENT_FALLING_EDGE));
    GWI_B(gwi_enum_end(gwi));

    gwidoc(gwi, "Open a gpiochip using smart function.");
    GWI_B(gwi_func_ini(gwi, "auto", "new"));
    GWI_B(gwi_func_arg(gwi, "string", "path"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_new, ae_flag_none));

    gwidoc(gwi, "Open a gpiochip by path.");
    GWI_B(gwi_func_ini(gwi, "Gpio", "open_by_path"));
    GWI_B(gwi_func_arg(gwi, "string", "path"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_open_by_path, ae_flag_static));

    gwidoc(gwi, "Open a gpiochip by name.");
    GWI_B(gwi_func_ini(gwi, "Gpio", "open_by_name"));
    GWI_B(gwi_func_arg(gwi, "string", "name"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_open_by_name, ae_flag_static));

    gwidoc(gwi, "Open a gpiochip by label.");
    GWI_B(gwi_func_ini(gwi, "Gpio", "open_by_label"));
    GWI_B(gwi_func_arg(gwi, "string", "label"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_open_by_label, ae_flag_static));

    gwidoc(gwi, "Open a gpiochip by number.");
    GWI_B(gwi_func_ini(gwi, "Gpio", "open_by_label"));
    GWI_B(gwi_func_arg(gwi, "string", "label"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_open_by_number, ae_flag_static));

    gwidoc(gwi, "Get the GPIO chip name as represented in the kernel.");
    GWI_B(gwi_func_ini(gwi, "string", "name"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_name, ae_flag_none));

    gwidoc(gwi, "Get the GPIO chip label as represented in the kernel.");
    GWI_B(gwi_func_ini(gwi, "string", "label"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_label, ae_flag_none));

    gwidoc(gwi, "Get the number of GPIO lines exposed by this chip.");
    GWI_B(gwi_func_ini(gwi, "int", "num_lines"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_num_lines, ae_flag_none));

    gwidoc(gwi, "Structure holding event info.");
    const Type t_lineev = gwi_class_ini(gwi, "LineEv", "Object");
    t_lineev->nspc->offset += sizeof(struct gpiod_line_event);
    SET_FLAG(t_lineev, abstract | ae_flag_final);

      GWI_B(gwi_func_ini(gwi, "dur", "ts"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_event_ts_get, ae_flag_none));
      GWI_B(gwi_func_ini(gwi, "EvType", "event_type"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_event_event_type_get, ae_flag_none));

    GWI_B(gwi_class_end(gwi)); // LineEv

    DECL_B(const Type, t_line, = gwi_class_ini(gwi, "Line", "Object"));
    t_line->nspc->offset += SZ_INT;
    SET_FLAG(t_line, abstract);

      gwidoc(gwi, "Read the GPIO line offset.");
      GWI_B(gwi_func_ini(gwi, "int", "offset"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_offset, ae_flag_none));

      gwidoc(gwi, "Read the GPIO line name.");
      GWI_B(gwi_func_ini(gwi, "string", "name"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_name, ae_flag_none));

      gwidoc(gwi, "Read the GPIO line consumer name.");
      GWI_B(gwi_func_ini(gwi, "string", "consumer"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_consumer, ae_flag_none));

      gwidoc(gwi, "Read current value of a single GPIO line.");
      GWI_B(gwi_func_ini(gwi, "int", "value"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_get_value, ae_flag_none));

      gwidoc(gwi, "Set the value of a single GPIO line.");
      GWI_B(gwi_func_ini(gwi, "int", "value"));
      GWI_B(gwi_func_arg(gwi, "int", "value"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_value, ae_flag_none));

      gwidoc(gwi, "Read the GPIO line direction setting.");
      GWI_B(gwi_func_ini(gwi, "Direction", "direction"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_direction, ae_flag_none));

      gwidoc(gwi, "Read the GPIO line state setting.");
      GWI_B(gwi_func_ini(gwi, "State", "state"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_state, ae_flag_none));

      gwidoc(gwi, "Read the GPIO line bias setting.");
      GWI_B(gwi_func_ini(gwi, "Bias", "bias"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_bias, ae_flag_none));

      gwidoc(gwi, "Check if the line is currently in use.");
      GWI_B(gwi_func_ini(gwi, "bool", "is_used"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_is_used, ae_flag_none));

      gwidoc(gwi, "Check if the line is a open drain gpio.");
      GWI_B(gwi_func_ini(gwi, "bool", "is_open_drain"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_is_open_drain, ae_flag_none));

      gwidoc(gwi, "Check if the line is a open source gpio.");
      GWI_B(gwi_func_ini(gwi, "bool", "is_open_source"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_is_open_source, ae_flag_none));

      gwidoc(gwi, "Update the configuration flags of a single GPIO line.");
      GWI_B(gwi_func_ini(gwi, "void", "flags"));
      GWI_B(gwi_func_arg(gwi, "Flag", "flags"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_flags, ae_flag_none));

      /*gwidoc(gwi, "Get the handle to the GPIO chip controlling this line.");
      GWI_B(gwi_func_ini(gwi, "Gpio", "get_chip"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_get_chip, ae_flag_none));*/

      #define mk_request(name, f, doc)                                      \
      gwidoc(gwi, doc);                                                     \
      GWI_B(gwi_func_ini(gwi, "void", #name));                           \
      GWI_B(gwi_func_arg(gwi, "string", "consumer"));                    \
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_request_##f, ae_flag_none)); \
      gwidoc(gwi, doc);    \
      GWI_B(gwi_func_ini(gwi, "void", #name));\
      GWI_B(gwi_func_arg(gwi, "string", "consumer"));\
      GWI_B(gwi_func_arg(gwi, "Flag", "flags"));\
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_request_##f##_flags, ae_flag_none)); \

      mk_request(input, input, "Reserve and set the direction to input.");
      mk_request(rising, rising_edge_events, "Request rising edge event.");
      mk_request(falling, falling_edge_events, "Request falling edge event.");
      mk_request(both_edges, both_edges_events, "Request all type events.");

      gwidoc(gwi, "Reserve and set the direction to output.");
      GWI_B(gwi_func_ini(gwi, "void", "output"));
      GWI_B(gwi_func_arg(gwi, "string", "consumer"));
      GWI_B(gwi_func_arg(gwi, "int", "default_val"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_request_output, ae_flag_none));

      gwidoc(gwi, "Reserve a single line, set the direction to output.  ");
      GWI_B(gwi_func_ini(gwi, "void", "output"));
      GWI_B(gwi_func_arg(gwi, "string", "consumer"));
      GWI_B(gwi_func_arg(gwi, "Flag", "flags"));
      GWI_B(gwi_func_arg(gwi, "int", "default_val"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_request_output_flags, ae_flag_none));

      gwidoc(gwi, "Update the configuration of a single GPIO line.");
      GWI_B(gwi_func_ini(gwi, "void", "config"));
      GWI_B(gwi_func_arg(gwi, "Direction", "state"));
      GWI_B(gwi_func_arg(gwi, "Flag", "flags"));
      GWI_B(gwi_func_arg(gwi, "int", "value"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_config, ae_flag_none));

      gwidoc(gwi, "Set the direction of a single GPIO line to input.");
      GWI_B(gwi_func_ini(gwi, "void", "set_input"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_direction_input, ae_flag_none));

      gwidoc(gwi, "Set the direction of a single GPIO line to output.");
      GWI_B(gwi_func_ini(gwi, "int", "set_output"));
      GWI_B(gwi_func_arg(gwi, "int", "value"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_direction_output, ae_flag_none));

      gwidoc(gwi, "Wait for an event on a single line.");
      GWI_B(gwi_func_ini(gwi, "void", "wait"));
      GWI_B(gwi_func_arg(gwi, "dur", "timeout"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_event_wait, ae_flag_none));

      gwidoc(gwi, "Release a previously reserved line.");
      GWI_B(gwi_func_ini(gwi, "void", "release"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_release, ae_flag_none));

      gwidoc(gwi, "check if the calling user has ownership of the line.");
      GWI_B(gwi_func_ini(gwi, "bool", "is_requested"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_is_requested, ae_flag_none));

      gwidoc(gwi, "check if the line is free.");
      GWI_B(gwi_func_ini(gwi, "bool", "is_free"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_is_free, ae_flag_none));

      gwidoc(gwi, "Read next pending event from the GPIO line.  ");
      GWI_B(gwi_func_ini(gwi, "LineEv", "read"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_event_read, ae_flag_none));

      gwidoc(gwi, "Read up to a certain number of events from the GPIO line.");
      GWI_B(gwi_func_ini(gwi, "LineEv[]", "read"));
      GWI_B(gwi_func_arg(gwi, "int", "num_events"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_event_read_multiple, ae_flag_none));

    gwi_class_end(gwi);

    gwidoc(gwi, "Get the handle to the GPIO line at given offset.");
    GWI_B(gwi_func_ini(gwi, "Line", "line"));
    GWI_B(gwi_func_arg(gwi, "int", "offset"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_get_line, ae_flag_none));

    gwidoc(gwi, "Find a Gpio line by name.");
    GWI_B(gwi_func_ini(gwi, "int", "find_line"));
    GWI_B(gwi_func_arg(gwi, "string", "name"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_find_line, ae_flag_none));

    gwidoc(gwi, "Find a set of Gpio lines by names.");
    GWI_B(gwi_func_ini(gwi, "int", "find_line"));
    GWI_B(gwi_func_arg(gwi, "string[]", "name"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_find_lines, ae_flag_none));

    gwidoc(gwi, "Holds several lines");
    const Type t_bulk = gwi_class_ini(gwi, "Bulk", "Object");
    t_bulk->nspc->offset += sizeof(struct gpiod_line_bulk);
    SET_FLAG(t_line, abstract);

      gwidoc(gwi, "Reset a bulk object. Remove all lines and set size to 0.");
      GWI_B(gwi_func_ini(gwi, "void", "reset"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_bulk_reset, ae_flag_none));

      // use <<
      gwidoc(gwi, "Add a single line to a GPIO bulk object.");
      GWI_B(gwi_func_ini(gwi, "void", "add_line"));
      GWI_B(gwi_func_arg(gwi, "Line", "line"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_bulk_add_line, ae_flag_none));

      gwidoc(gwi, "Retrieve the number of GPIO lines held by this line bulk object.");
      GWI_B(gwi_func_ini(gwi, "int", "num_lines"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_bulk_num_lines, ae_flag_none));

      mk_request(input, bulk_input, "Reserve and set the direction to input.");
      mk_request(rising, bulk_rising_edge_events, "Request rising edge event.");
      mk_request(falling, bulk_falling_edge_events, "Request falling edge event.");
      mk_request(both_edges, bulk_both_edges_events, "Request all type events.");

      gwidoc(gwi, "Reserve a set of GPIO lines, set the direction to output.");
      GWI_B(gwi_func_ini(gwi, "void", "output"));
      GWI_B(gwi_func_arg(gwi, "string", "consumer"));
      GWI_B(gwi_func_arg(gwi, "u32[]", "default_vals"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_request_bulk_output, ae_flag_none));

      gwidoc(gwi, "Update the configuration of a set of GPIO lines.");
      GWI_B(gwi_func_ini(gwi, "void", "config"));
      GWI_B(gwi_func_arg(gwi, "Direction", "state"));
      GWI_B(gwi_func_arg(gwi, "Flag", "flags"));
      GWI_B(gwi_func_arg(gwi, "u32[]", "values"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_config_bulk, ae_flag_none));

      gwidoc(gwi, "Reserve a set of GPIO lines, set the direction to output.");
      GWI_B(gwi_func_ini(gwi, "void", "output"));
      GWI_B(gwi_func_arg(gwi, "string", "consumer"));
      GWI_B(gwi_func_arg(gwi, "Flag", "flags"));
      GWI_B(gwi_func_arg(gwi, "u32[]", "default_vals"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_request_bulk_output_flags, ae_flag_none));

      gwidoc(gwi, "Update the configuration flags of a set of GPIO lines.");
      GWI_B(gwi_func_ini(gwi, "void", "flags"));
      GWI_B(gwi_func_arg(gwi, "Flag", "flags"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_flags_bulk, ae_flag_none));

      gwidoc(gwi, "Set the direction of a set of GPIO lines to input.");
      GWI_B(gwi_func_ini(gwi, "void", "set_input"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_direction_input_bulk, ae_flag_none));

      gwidoc(gwi, "Read current values of a set of GPIO lines.");
      GWI_B(gwi_func_ini(gwi, "u32[]", "value"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_get_value_bulk, ae_flag_none));

      gwidoc(gwi, "Set the values of a set of GPIO lines.");
      GWI_B(gwi_func_ini(gwi, "void", "value"));
      GWI_B(gwi_func_arg(gwi, "u32[]", "values"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_value_bulk, ae_flag_none));

      gwidoc(gwi, "Set the direction of a set of GPIO lines to output.");
      GWI_B(gwi_func_ini(gwi, "void", "set_output"));
      GWI_B(gwi_func_arg(gwi, "u32[]", "values"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_set_direction_output_bulk, ae_flag_none));

      gwidoc(gwi, "Release a set of previously reserved lines.  ");
      GWI_B(gwi_func_ini(gwi, "void", "release"));
      GWI_B(gwi_func_arg(gwi, "Bulk", "bulk"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_release_bulk, ae_flag_none));

      gwidoc(gwi, "Retrieve the line handle from a line bulk object at given index.  ");
      GWI_B(gwi_func_ini(gwi, "Line", "get_line"));
      GWI_B(gwi_func_arg(gwi, "int", "index"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_bulk_get_line, ae_flag_none));

      gwidoc(gwi, "Wait for events on a set of lines.");
      GWI_B(gwi_func_ini(gwi, "void", "wait"));
      GWI_B(gwi_func_arg(gwi, "dur", "timeout"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_event_wait_bulk, ae_flag_none));
/*

      gwidoc(gwi, "Wait for events on a set of lines.");
      GWI_B(gwi_func_ini(gwi, "bool", "wait"));
      GWI_B(gwi_func_arg(gwi, "dur", "timeout"));
      GWI_B(gwi_func_arg(gwi, "Bulk", "event_bulk"));
      GWI_B(gwi_func_end(gwi, gw_gpiod_line_event_wait_bulk2, ae_flag_none));
*/
    gwi_class_end(gwi);

/*

  // implement foreach instead?
  gwidoc(gwi, "Iterate over all lines held by this bulk object.  ");
  GWI_B(gwi_func_ini(gwi, "void", "gpiod_line_bulk_foreach_line"));
  GWI_B(gwi_func_arg(gwi, "int (gpiod_line *,void *)", "func"));
  GWI_B(gwi_func_arg(gwi, "void", "data"));
  GWI_B(gwi_func_end(gwi, gw_gpiod_line_bulk_foreach_line, ae_flag_none));
*/
    gwidoc(gwi, "Retrieve a set of lines and store them in a line bulk object.");
    GWI_B(gwi_func_ini(gwi, "Bulk", "lines"));
    GWI_B(gwi_func_arg(gwi, "u32[]", "offsets"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_get_lines, ae_flag_none));

    gwidoc(gwi, "Retrieve all lines exposed by a chip and store them in a bulk object.");
    GWI_B(gwi_func_ini(gwi, "Bulk", "lines"));
    GWI_B(gwi_func_end(gwi, gw_gpiod_chip_get_all_lines, ae_flag_none));

  GWI_B(gwi_class_end(gwi));

  gwidoc(gwi, "Wait for an event");
  GWI_B(gwi_oper_ini(gwi, "Gpio.Line", "Now", "void"));
  GWI_B(gwi_oper_end(gwi, "=>", line2now));

  gwidoc(gwi, "Wait for an event");
  GWI_B(gwi_oper_ini(gwi, "Gpio.Bulk", "Now", "void"));
  GWI_B(gwi_oper_end(gwi, "=>", bulk2now));

  // bulk array access?
  // bulk << line?
  return true;
}
