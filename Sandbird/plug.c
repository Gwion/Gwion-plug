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
#include "ugen.h"
#include "array.h"
#include "sandbird.h"

MK_VECTOR_TYPE(sb_Event, sbevent)

#define SRV(o) (*(sb_Server**)(o->data + SZ_INT))
#define EV(o) (*(sb_EventList**)(o->data + SZ_INT*2))
#define ST(o) (*(sb_Stream**)(o->data))

static int handler(sb_Event*ev) {
  const M_Object o = (M_Object)ev->udata;
  if(!vector_size(&EV_SHREDS(o))) return;
  const VM_Shred shred = (VM_Shred)vector_front(&EV_SHREDS(o));
  sbeventlist_set(EV(o), 0, *ev);
  broadcast(o);
  return 0;
}

static void process(void *data) {
  const M_Object o = data;
  for (;;) {
    sb_poll_server(SRV(o), 1000);
  }
}

static CTOR(sandbird_ctor) {
  sb_Options opt = {
    .udata = o,
    .port = "8000",
    .handler = handler,
  };
  SRV(o) = sb_new_server(&opt);
  EV(o) = new_sbeventlist(shred->info->mp, 1);
  threadpool_add(shred, process, o); // long running
}

static DTOR(sandbird_dtor) {
  sb_close_server(SRV(o));
  free_sbeventlist(shred->info->mp, EV(o));
}

static SFUN(error_str) {
  const int code = *(m_int*)MEM(SZ_INT);
  const char *error = sb_error_str(code);
  *(M_Object*)RETURN = new_string(shred->info->vm->gwion, (char*)error);
}

static MFUN(send_status) {
  const int code = *(m_int*)MEM(SZ_INT);
  M_Object msg = *(M_Object*)MEM(SZ_INT*2);
  *(m_int*)RETURN = sb_send_status(ST(o), code, STRING(msg));
}

static MFUN(send_header) {
  M_Object field = *(M_Object*)MEM(SZ_INT);
  M_Object val   = *(M_Object*)MEM(SZ_INT*2);
  *(m_int*)RETURN = sb_send_header(ST(o), STRING(field), STRING(val));
}

static MFUN(send_file) {
  M_Object filename = *(M_Object*)MEM(SZ_INT);
  *(m_int*)RETURN = sb_send_file(ST(o), STRING(filename));
}

static MFUN(write) {
  M_Object filename = *(M_Object*)MEM(SZ_INT);
  const m_str data = STRING(filename);
  *(m_int*)RETURN = sb_write(ST(o), data, strlen(data));
}
static int i;

static MFUN(recv) {
  M_Object stream = *(M_Object*)MEM(SZ_INT);
  if(!EV(o)->len) {
     *(m_int*)RETURN = 0;
     return;
  }
  const sb_Event ev = sbeventlist_at(EV(o), 0);
//printf("!stream %p\n", ev->stream);
  *(sb_Stream**)(stream->data) = ev.stream;
  *(m_int*)(stream->data + SZ_INT) = ev.type;
  const Gwion gwion = shred->info->vm->gwion;
//  if(ev->address)
//  *(M_Object*)(stream->data + SZ_INT*2) = new_string(gwion, (m_str)ev->address);
//  if(ev->address)
//  *(M_Object*)(stream->data + SZ_INT*3) = new_string(gwion, (m_str)ev->method);
//  if(ev->address)
//  *(M_Object*)(stream->data + SZ_INT*4) = new_string(gwion, (m_str)ev->path);
//  *(m_int*)RETURN = 1;
//  const VM_Code code = *(VM_Code*) REG(-SZ_INT);
//  M_Object filename = new_object(shred->info->vm->gwion, code->ret_type);
// *(M_Object*)MEM(SZ_INT);
//  const m_str data = STRING(filename);
//  *(m_int*)RETURN = sb_write(ST(o), data, strlen(data));
}


// sb_get_header
// sb_get_var
// sb_get_cookie
// void *sb_get_multipart

GWION_IMPORT(Sandbird) {
  DECL_B(const Type, t_sandbird, = gwi_class_ini(gwi, "Sandbird", "Event"));
  t_sandbird->nspc->offset += SZ_INT*2;
  gwi_class_xtor(gwi, sandbird_ctor, sandbird_dtor);

  GWI_B(gwi_enum_ini(gwi, "Status"));
  GWI_B(gwi_enum_add(gwi, "sucess", SB_ESUCCESS));
  GWI_B(gwi_enum_add(gwi, "failure", SB_EFAILURE));
  GWI_B(gwi_enum_add(gwi, "outofmem", SB_EOUTOFMEM));
  GWI_B(gwi_enum_add(gwi, "truncated", SB_ETRUNCATED));
  GWI_B(gwi_enum_add(gwi, "badstate", SB_EBADSTATE));
  GWI_B(gwi_enum_add(gwi, "badresult", SB_EBADRESULT));
  GWI_B(gwi_enum_add(gwi, "cantopen", SB_ECANTOPEN));
  GWI_B(gwi_enum_add(gwi, "notfound", SB_ENOTFOUND));
  GWI_B(gwi_enum_add(gwi, "fdtoobig", SB_EFDTOOBIG));
  GWI_B(gwi_enum_end(gwi));

  GWI_B(gwi_enum_ini(gwi, "Type"));
  GWI_B(gwi_enum_add(gwi, "connect", SB_EV_CONNECT));
  GWI_B(gwi_enum_add(gwi, "close", SB_EV_CLOSE));
  GWI_B(gwi_enum_add(gwi, "request", SB_EV_REQUEST));
  GWI_B(gwi_enum_end(gwi));

  GWI_B(gwi_enum_ini(gwi, "res"));
  GWI_B(gwi_enum_add(gwi, "ok", SB_RES_OK));
  GWI_B(gwi_enum_add(gwi, "close", SB_RES_CLOSE));
  GWI_B(gwi_enum_end(gwi));

  GWI_B(gwi_func_ini(gwi, "string", "errorstr"));
  GWI_B(gwi_func_arg(gwi, "int", "code"));
  GWI_B(gwi_func_end(gwi, error_str, ae_flag_static));

  DECL_B(const Type, t_event, = gwi_class_ini(gwi, "Event", "Object"));
  t_event->nspc->offset += SZ_INT;
  GWI_B(gwi_item_ini(gwi, "int", "type"));
  GWI_B(gwi_item_end(gwi, ae_flag_const, obj, 0));
  GWI_B(gwi_item_ini(gwi, "string", "address"));
  GWI_B(gwi_item_end(gwi, ae_flag_const, obj, 0));
  GWI_B(gwi_item_ini(gwi, "string", "method"));
  GWI_B(gwi_item_end(gwi, ae_flag_const, obj, 0));
  GWI_B(gwi_item_ini(gwi, "string", "path"));
  GWI_B(gwi_item_end(gwi, ae_flag_const, obj, 0));

  GWI_B(gwi_func_ini(gwi, "int", "send_status"));
  GWI_B(gwi_func_arg(gwi, "int", "code"));
  GWI_B(gwi_func_arg(gwi, "string", "msg"));
  GWI_B(gwi_func_end(gwi, send_status, ae_flag_none));

  GWI_B(gwi_func_ini(gwi, "int", "send_header"));
  GWI_B(gwi_func_arg(gwi, "string", "field"));
  GWI_B(gwi_func_arg(gwi, "string", "val"));
  GWI_B(gwi_func_end(gwi, send_header, ae_flag_none));

  GWI_B(gwi_func_ini(gwi, "int", "send_file"));
  GWI_B(gwi_func_arg(gwi, "string", "filename"));
  GWI_B(gwi_func_end(gwi, send_file, ae_flag_none));

  GWI_B(gwi_func_ini(gwi, "int", "write"));
  GWI_B(gwi_func_arg(gwi, "string", "data"));
  GWI_B(gwi_func_end(gwi, write, ae_flag_none));

  GWI_B(gwi_class_end(gwi));

//  GWI_B(gwi_func_ini(gwi, "int", "recv"));
  GWI_B(gwi_func_ini(gwi, "Type", "recv"));
  GWI_B(gwi_func_arg(gwi, "Event", "ev"));
  GWI_B(gwi_func_end(gwi, recv, ae_flag_none));

  GWI_B(gwi_class_end(gwi));
  return true;

}
