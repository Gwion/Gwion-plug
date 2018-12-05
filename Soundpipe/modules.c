#include <stdlib.h>
#include <math.h>
#include <soundpipe.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "env.h"
#include "vm.h"
#include "type.h"
#include "instr.h"
#include "object.h"
#include "import.h"
#include "ugen.h"
#include "func.h"

typedef struct SP_osc_ {
  sp_data* sp;
  sp_osc*  osc;
  sp_ftbl* tbl;
  m_float  phz;
} SP_osc; // copied from generated osc.c

extern sp_data* sp;
static TICK(sinosc_tick) {
  const SP_osc* ug = (SP_osc*)u->module.gen.data;
  sp_osc_compute(ug->sp, ug->osc, NULL, &u->out);
}

ANN static void refresh_sine(const VM* vm, SP_osc* ug, const m_int sz, const m_float phz) {
  if(sz <= 0) {
    err_msg(0, "%s size requested for sinosc. doing nothing",
            sz < 0 ? "negative" : "zero");
    return;
  }
  sp_ftbl_destroy(&ug->tbl);
  sp_osc_destroy(&ug->osc);
  sp_osc_create(&ug->osc);
  sp_ftbl_create(sp, &ug->tbl, sz);
  sp_gen_sine(sp, ug->tbl);
  sp_osc_init(sp, (sp_osc*)ug->osc, ug->tbl, phz);
}

static CTOR(sinosc_ctor) {
  SP_osc* ug = (SP_osc*)xmalloc(sizeof(SP_osc));
  sp_osc_create(&ug->osc);
  sp_ftbl_create(sp, &ug->tbl, 2048);
  sp_gen_sine(sp, ug->tbl);
  sp_osc_init(sp, ug->osc, ug->tbl, 0.);
  ugen_ini(UGEN(o), 0, 1);
  ugen_gen(UGEN(o), sinosc_tick, ug, 0);
}

static DTOR(sinosc_dtor) {
  SP_osc* ug = (SP_osc*)UGEN(o)->module.gen.data;
  sp_osc_destroy(&ug->osc);
  sp_ftbl_destroy(&ug->tbl);
  free(ug);
}

static MFUN(sinosc_size) {
  const m_int size = *(m_int*)(shred->mem + SZ_INT);
  SP_osc* ug = (SP_osc*)UGEN(o)->module.gen.data;
  refresh_sine(shred->vm, ug, size, 0);
}

static MFUN(sinosc_size_phase) {
  const m_int size    = *(m_int*)(shred->mem + SZ_INT);
  const float phase = *(m_int*)(shred->mem + SZ_INT * 2);
  SP_osc* ug = (SP_osc*)UGEN(o)->module.gen.data;
  refresh_sine(shred->vm, ug, size, phase);
}

static MFUN(sinosc_get_freq) {
  const SP_osc* ug = (SP_osc*)UGEN(o)->module.gen.data;
  *(m_float*)RETURN = ug->osc->freq;
}

static MFUN(sinosc_set_freq) {
  const SP_osc* ug = (SP_osc*)UGEN(o)->module.gen.data;
  const m_float freq = *(m_float*)(shred->mem + SZ_INT);
  *(m_float*)RETURN = (ug->osc->freq = freq);
}

static MFUN(sinosc_get_amp) {
  const SP_osc* ug = (SP_osc*)UGEN(o)->module.gen.data;
  *(m_float*)RETURN = ug->osc->amp;
}

static MFUN(sinosc_set_amp) {
  const SP_osc* ug = (SP_osc*)UGEN(o)->module.gen.data;
  const m_float amp = *(m_float*)(shred->mem + SZ_INT);
  *(m_float*)RETURN = (ug->osc->amp = amp);
}

ANN static m_bool import_sinosc(const Gwi gwi) {
  const Type t_sinosc = gwi_mk_type(gwi, "SinOsc", SZ_INT, t_ugen);
  CHECK_BB(gwi_class_ini(gwi,  t_sinosc, sinosc_ctor, sinosc_dtor))
  gwi_func_ini(gwi, "void", "init", sinosc_size);
  gwi_func_arg(gwi, "int", "size");
  CHECK_BB(gwi_func_end(gwi, 0))
  gwi_func_ini(gwi, "void", "init", sinosc_size_phase);
  gwi_func_arg(gwi, "int", "size");
  gwi_func_arg(gwi, "float", "phase");
  CHECK_BB(gwi_func_end(gwi, 0))
  gwi_func_ini(gwi, "float", "freq", sinosc_get_freq);
  CHECK_BB(gwi_func_end(gwi, 0))
  gwi_func_ini(gwi, "float", "freq", sinosc_set_freq);
  gwi_func_arg(gwi, "float", "freq");
  CHECK_BB(gwi_func_end(gwi, 0))
  gwi_func_ini(gwi, "float", "amp", sinosc_get_amp);
  CHECK_BB(gwi_func_end(gwi, 0))
  gwi_func_ini(gwi, "float", "amp", sinosc_set_amp);
  gwi_func_arg(gwi, "float", "amp");
  CHECK_BB(gwi_func_end(gwi, 0))
  CHECK_BB(gwi_class_end(gwi))
  return 1;
}

static DTOR(basic_dtor) {
  free(UGEN(o)->module.gen.data);
}

static TICK(gain_tick) {
  u->out = (u->in * *(m_float*)u->module.gen.data);
}

static CTOR(gain_ctor) {
  ugen_ini(UGEN(o), 1, 1);
  ugen_gen(UGEN(o), gain_tick, (m_float*)xmalloc(SZ_FLOAT), 0);
  UGEN(o)->module.gen.tick = gain_tick;
  *(m_float*)UGEN(o)->module.gen.data = 1;
}


static MFUN(gain_get_gain) {
  *(m_float*)RETURN = *(m_float*)UGEN(o)->module.gen.data;
}

static MFUN(gain_set_gain) {
  *(m_float*)RETURN = *(m_float*)UGEN(o)->module.gen.data = *(m_float*)MEM(SZ_INT);
}

ANN static m_bool import_gain(const Gwi gwi) {
  const Type t_gain = gwi_mk_type(gwi, "Gain", SZ_INT, t_ugen);
  CHECK_BB(gwi_class_ini(gwi,  t_gain, gain_ctor, basic_dtor))
  gwi_func_ini(gwi, "float", "gain", gain_get_gain);
  CHECK_BB(gwi_func_end(gwi, 0))
  gwi_func_ini(gwi, "float", "gain", gain_set_gain);
  gwi_func_arg(gwi, "float", "arg0");
  CHECK_BB(gwi_func_end(gwi, 0))
  CHECK_BB(gwi_class_end(gwi))
  return 1;
}

static TICK(impulse_tick) {
  u->out = *(m_float*)u->module.gen.data;
  *(m_float*)u->module.gen.data = 0;
}

static CTOR(impulse_ctor) {
  ugen_ini(UGEN(o), 0, 1);
  ugen_gen(UGEN(o), impulse_tick, (m_float*)xmalloc(SZ_FLOAT), 0);
  *(m_float*)UGEN(o)->module.gen.data = 0;
}

static MFUN(impulse_get_next) {
  *(m_float*)RETURN = *(m_float*)UGEN(o)->module.gen.data;
}

static MFUN(impulse_set_next) {
  *(m_float*)RETURN = (*(m_float*)UGEN(o)->module.gen.data = *(m_float*)MEM(SZ_INT));
}

ANN static m_bool import_impulse(const Gwi gwi) {
  const Type t_impulse = gwi_mk_type(gwi, "Impulse", SZ_INT, t_ugen);
  CHECK_BB(gwi_class_ini(gwi,  t_impulse, impulse_ctor, basic_dtor))
  gwi_func_ini(gwi, "float", "next", impulse_get_next);
  CHECK_BB(gwi_func_end(gwi, 0))
  gwi_func_ini(gwi, "float", "next", impulse_set_next);
  gwi_func_arg(gwi, "float", "arg0");
  CHECK_BB(gwi_func_end(gwi, 0))
  CHECK_BB(gwi_class_end(gwi))
  return 1;
}

static TICK(fullrect_tick) {
  u->out = fabs(u->in);
}

static CTOR(fullrect_ctor) {
  ugen_ini(UGEN(o), 1, 1);
  ugen_gen(UGEN(o), fullrect_tick, (m_float*)xmalloc(SZ_FLOAT), 0);
  *(m_float*)UGEN(o)->module.gen.data = 1;
}

ANN static m_bool import_fullrect(const Gwi gwi) {
  const Type t_fullrect = gwi_mk_type(gwi, "FullRect", SZ_INT, t_ugen);
  CHECK_BB(gwi_class_ini(gwi,  t_fullrect, fullrect_ctor, basic_dtor))
  CHECK_BB(gwi_class_end(gwi))
  return 1;
}

static TICK(halfrect_tick) {
  if(u->in > 0)
    u->out = u->in;
  else
    u->out = 0;
}

static CTOR(halfrect_ctor) {
  ugen_ini(UGEN(o), 1, 1);
  ugen_gen(UGEN(o), halfrect_tick, (m_float*)xmalloc(SZ_FLOAT), 0);
  *(m_float*)UGEN(o)->module.gen.data = 1;
}

ANN static m_bool import_halfrect(const Gwi gwi) {
  const Type t_halfrect = gwi_mk_type(gwi, "HalfRect", SZ_INT, t_ugen);
  CHECK_BB(gwi_class_ini(gwi,  t_halfrect, halfrect_ctor, basic_dtor))
  CHECK_BB(gwi_class_end(gwi))
  return 1;
}

static TICK(step_tick) {
  u->out = *(m_float*)u->module.gen.data;
}

static CTOR(step_ctor) {
  ugen_ini(UGEN(o), 0, 1);
  ugen_gen(UGEN(o), step_tick, (m_float*)xmalloc(SZ_FLOAT), 0);
  *(m_float*)UGEN(o)->module.gen.data = 0;
}

static MFUN(step_get_next) {
  *(m_float*)RETURN = *(m_float*)UGEN(o)->module.gen.data;
}

static MFUN(step_set_next) {
  *(m_float*)RETURN = *(m_float*)UGEN(o)->module.gen.data = *(m_float*)(shred->mem + SZ_INT);
}

ANN static m_bool import_step(const Gwi gwi) {
  const Type t_step = gwi_mk_type(gwi, "Step", SZ_INT, t_ugen);
  CHECK_BB(gwi_class_ini(gwi,  t_step, step_ctor, basic_dtor))
  gwi_func_ini(gwi, "float", "next", step_get_next);
  CHECK_BB(gwi_func_end(gwi, 0))
  gwi_func_ini(gwi, "float", "next", step_set_next);
  gwi_func_arg(gwi, "float", "arg0");
  CHECK_BB(gwi_func_end(gwi, 0))
  CHECK_BB(gwi_class_end(gwi))
  return 1;
}

static TICK(zerox_tick) {
  m_float in = (u->in < 0) ? -1 : (u->in > 0);
  m_float f = *(m_float*)u->module.gen.data;
  u->out = f == in ? 1 : 0;
  *(m_float*) u->module.gen.data = in;
}

static CTOR(zerox_ctor) {
  ugen_ini(UGEN(o), 1, 1);
  ugen_gen(UGEN(o), zerox_tick, (m_float*)xmalloc(SZ_FLOAT), 0);
  *(m_float*)UGEN(o)->module.gen.data = 1;
}

ANN static m_bool import_zerox(const Gwi gwi) {
  const Type t_zerox = gwi_mk_type(gwi, "ZeroX", SZ_INT, t_ugen);
  CHECK_BB(gwi_class_ini(gwi, t_zerox, zerox_ctor, basic_dtor))
  CHECK_BB(gwi_class_end(gwi))
  return 1;
}

//GWION_IMPORT(modules) {
m_bool import_modules(const Gwi gwi) {
  CHECK_BB(import_sinosc(gwi))
  CHECK_BB(import_gain(gwi))
  CHECK_BB(import_impulse(gwi))
  CHECK_BB(import_fullrect(gwi))
  CHECK_BB(import_halfrect(gwi))
  CHECK_BB(import_step(gwi))
  CHECK_BB(import_zerox(gwi))
  return 1;
}
