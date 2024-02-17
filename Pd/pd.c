#include <libpd/z_libpd.h>
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

static bool pd_init;

#define PDFILE(o) (*(void**)(o->data + SZ_INT))

static CTOR(pd_ctor) {
  if(!pd_init++) {
    VM* vm = shred->info->vm;
    libpd_init();
    libpd_init_audio(vm->bbq->si->in , vm->bbq->si->out, vm->bbq->si->sr);
    libpd_start_message(256);
    libpd_add_float(1.0);
    libpd_finish_message ("pd" , "dsp");
  }
  PDFILE(o) = NULL;
}

static DTOR(pd_dtor) {
  if(PDFILE(o))
    libpd_closefile(PDFILE(o));
}

static MFUN(gwpd_open) {
  M_Object f_obj = *(M_Object*)MEM(SZ_INT);
  M_Object d_obj = *(M_Object*)MEM(SZ_INT*2);
  PDFILE(o) = libpd_openfile(STRING(f_obj), STRING(d_obj));
}

static MFUN(gwpd_close) {
  if(PDFILE(o))
    libpd_closefile(PDFILE(o));
}

GWION_IMPORT(Pd) {
  DECL_B(const Type, t_pd, = gwi_class_ini(gwi, "PD", "UGen"));
  gwi_class_xtor(gwi, pd_ctor, pd_dtor);
  t_pd->nspc->offset += SZ_INT;
  GWI_B(gwi_func_ini(gwi, "int", "open"))
  GWI_B(gwi_func_arg(gwi, "string", "basename"))
  GWI_B(gwi_func_arg(gwi, "string", "dirname"))
  GWI_B(gwi_func_end(gwi, gwpd_open, ae_flag_none))
  GWI_B(gwi_func_ini(gwi, "int", "close"))
  GWI_B(gwi_func_end(gwi, gwpd_close, ae_flag_none))
  GWI_B(gwi_class_end(gwi))
  return true;
}
