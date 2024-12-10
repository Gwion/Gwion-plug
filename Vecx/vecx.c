#include <complex.h>
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
#include "gack.h"
#include "emit.h"
#include "traverse.h"
#include "parse.h"

#define SZ_COMPLEX (SZ_FLOAT*2)
#define SZ_VEC3    (SZ_FLOAT*3)
#define SZ_VEC4    (SZ_FLOAT*4)

#define describe(name, op) \
static INSTR(Complex##name) {\
  POP_REG(shred, SZ_COMPLEX); \
  *(m_complex*)REG(-SZ_COMPLEX) op##= *(m_complex*)REG(0); \
}
describe(Add,   +)
describe(Sub,  -)
describe(Mul,  *)
describe(Div, /)

static INSTR(ComplexRAssign) {
  POP_REG(shred, SZ_INT);
  **(m_complex**)REG(0) = *(m_complex*)REG(-SZ_COMPLEX);
}

#define describe_r(name, op) \
static INSTR(ComplexR##name) {\
  POP_REG(shred, SZ_INT); \
  *(m_complex*)REG(-SZ_COMPLEX) = (**(m_complex**)REG(0) op##= (*(m_complex*)REG(-SZ_COMPLEX))); \
}
describe_r(Add,  +)
describe_r(Sub, -)
describe_r(Mul, *)
describe_r(Div, /)

INSTR(ComplexReal) {
//  if(!instr->m_val) { // other case skipped in emit.c
    *(m_float*)REG(-SZ_INT) = **(m_float**)REG(-SZ_INT);
    PUSH_REG(shred, SZ_FLOAT - SZ_INT);
//  }
}

INSTR(ComplexImag) {
  if(instr->m_val) {
    const m_float* f = *(m_float**)REG(-SZ_INT);
    *(const m_float**)REG(-SZ_INT) = (f + 1);
  } else {
    const m_float* f = *(m_float**)REG(-SZ_INT);
    *(m_float*)REG(-SZ_INT) = *(f + 1);
    PUSH_REG(shred, SZ_FLOAT - SZ_INT);
  }
}

#if defined __clang__ && defined BUILD_ON_WINDOWS
#define POLAR(a, b, c) m_complex a = { b, c };
#else
#define POLAR(a, b, c) m_complex a = b + c *I;
#endif

#define polar_def1(name, op)                                               \
static INSTR(Polar##name) {                                                \
  POP_REG(shred, SZ_COMPLEX);                                              \
  const m_complex a = *(m_complex*)REG(-SZ_COMPLEX);                       \
  const m_complex b = *(m_complex*)REG(0);                                 \
  const m_float re = creal(a) * cos(cimag(a)) op creal(b) * cos(cimag(b)); \
  const m_float im = creal(a) * sin(cimag(a)) op creal(b) * sin(cimag(b)); \
  POLAR(c, hypot(re, im), atan2(im, re))                                   \
  *(m_complex*)REG(-SZ_COMPLEX) = c;                                       \
}

polar_def1(Add,  +)
polar_def1(Sub, -)

#define polar_def2(name, op1, op2)                   \
static INSTR(Polar##name) {\
  POP_REG(shred, SZ_COMPLEX);                        \
  const m_complex a = *(m_complex*)REG(-SZ_COMPLEX); \
  const m_complex b = *(m_complex*)REG(0);           \
  const m_float mag   = creal(a) op1 creal(b);       \
  const m_float phase = cimag(a) op2 cimag(b);       \
  POLAR(c, mag, phase);                              \
  *(m_complex*)REG(-SZ_COMPLEX) = c;                 \
}
polar_def2(Mul, *, +)
polar_def2(Div, /, -)

#define polar_def1_r(name, op)                                             \
static INSTR(PolarR##name) {\
  POP_REG(shred, SZ_INT);                                                  \
  const m_complex a = *(m_complex*)REG(-SZ_COMPLEX);                       \
  const m_complex b = **(m_complex**)REG(0);                               \
  const m_float re = creal(a) * cos(cimag(a)) op creal(b) * cos(cimag(b)); \
  const m_float im = creal(a) * sin(cimag(a)) op creal(b) * sin(cimag(b)); \
  POLAR(c, hypot(re, im), atan2(im, re))                                   \
  *(m_complex*)REG(-SZ_COMPLEX) = **(m_complex**)REG(0) = c;               \
}
polar_def1_r(Add, +)
polar_def1_r(Sub, -)

#define polar_def2_r(name, op1, op2)                         \
static INSTR(PolarR##name) {                                 \
  POP_REG(shred, SZ_INT);                                    \
  const m_complex a = *(m_complex*)REG(-SZ_COMPLEX);         \
  const m_complex b = **(m_complex**)REG(0);                 \
  const m_float mag   = creal(a) op1 creal(b);               \
  const m_float phase = cimag(a) op2 cimag(b);               \
  POLAR(c, mag, phase);                                      \
  *(m_complex*)REG(-SZ_COMPLEX) = **(m_complex**)REG(0) = c; \
}
polar_def2_r(Mul, *, +)
polar_def2_r(Div, /, -)

static GACK(gack_complex) {
  m_float *ptr = (m_float*)VALUE;
  INTERP_PRINTF("complex(%.4f, %.4f)", *ptr, *(ptr + 1));
}

static GACK(gack_polar) {
  m_float *ptr = (m_float*)VALUE;
  INTERP_PRINTF("polar(%4f, %.4f*pi)", *ptr, *(ptr+1) / M_PI);
}

EQUALITY_OPER(complex, SZ_COMPLEX)
OP_CHECK(opck_vecx_ctor);
/*
#define opem(type, first_name) static OP_EMIT(opem_##type##_dot) {               \
  const Exp_Dot *dot = (Exp_Dot*)data;                                           \
  Exp* base = dot->base;                                                    \
  exp_setvar(base, 1);                                                            \
  if(!emit_exp(emit, base)) return (Instr)true;                              \
  const bool is_first = !strcmp(#first_name, s_name(dot->tag.sym));                \
  if(is_first && exp_self(dot)->emit_var)                                        \
    return (Instr)true;                                                         \
  const Instr instr = emit_add_instr(emit, is_first ? ComplexReal : ComplexImag);\
  instr->m_val = exp_getvar(exp_self(dot));                                        \
  return (Instr)true;                                                           \
}
opem(complex, re)
opem(polar, mod)

OP_CHECK(opck_object_dot);
*/

static MFUN(vec2_set) {
  **(m_complex**)MEM(0) = cexpf(
       *(m_float*)MEM(SZ_INT) +
       *(m_float*)MEM(SZ_INT + SZ_FLOAT) * I);
}

static GWION_IMPORT(complex) {
  const Type t_complex = gwi_struct_ini(gwi, "complex");
SET_FLAG(t_complex, abstract);
  GWI_B(gwi_gack(gwi, t_complex, gack_complex))
  gwi_item_ini(gwi, "float", "re");
  GWI_B(gwi_item_end(gwi,   ae_flag_none, num, 0))
  gwi_item_ini(gwi, "float", "im");
  GWI_B(gwi_item_end(gwi,   ae_flag_none, num, 0))
  gwi_func_ini(gwi, "auto", "new");
  gwi_func_arg(gwi, "float", "re");
  gwi_func_arg(gwi, "float", "im");
  GWI_B(gwi_func_end(gwi, vec2_set, ae_flag_none))

  GWI_B(gwi_class_end(gwi))
  const Type t_polar   = gwi_struct_ini(gwi, "polar");
  GWI_B(gwi_gack(gwi, t_polar, gack_polar))
  GWI_B(gwi_item_ini(gwi, "float", "mod"))
  GWI_B(gwi_item_end(gwi,   ae_flag_none, num, 0))
  GWI_B(gwi_item_ini(gwi, "float", "phase"))
  GWI_B(gwi_item_end(gwi,   ae_flag_none, num, 0))
  gwi_func_ini(gwi, "auto", "new");
  gwi_func_arg(gwi, "float", "mod");
  gwi_func_arg(gwi, "float", "phase");
  GWI_B(gwi_func_end(gwi, vec2_set, ae_flag_none))
  GWI_B(gwi_class_end(gwi))

   GWI_B(gwi_oper_ini(gwi, "complex", "complex", "bool"))
   GWI_B(gwi_oper_end(gwi, "==",          complex_eq))
   GWI_B(gwi_oper_end(gwi, "!=",          complex_ne))
   GWI_B(gwi_oper_ini(gwi, "complex", "complex", "complex"))
   GWI_B(gwi_oper_end(gwi, "+",          ComplexAdd))
   GWI_B(gwi_oper_end(gwi, "-",         ComplexSub))
   GWI_B(gwi_oper_end(gwi, "*",         ComplexMul))
   GWI_B(gwi_oper_end(gwi, "/",        ComplexDiv))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, ":=>",         ComplexRAssign))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, "+=>",    ComplexRAdd))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, "-=>",   ComplexRSub))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, "*=>",   ComplexRMul))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, "/=>",  ComplexRDiv))
   GWI_B(gwi_oper_ini(gwi, "polar", "polar", "bool"))
   GWI_B(gwi_oper_end(gwi, "==",          complex_eq))
   GWI_B(gwi_oper_end(gwi, "!=",          complex_ne))
   GWI_B(gwi_oper_ini(gwi, "polar", "polar", "polar"))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, ":=>",         ComplexRAssign))
   GWI_B(gwi_oper_end(gwi, "+",          PolarAdd))
   GWI_B(gwi_oper_end(gwi, "-",         PolarSub))
   GWI_B(gwi_oper_end(gwi, "*",         PolarMul))
   GWI_B(gwi_oper_end(gwi, "/",        PolarDiv))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, "+=>",    PolarRAdd))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, "-=>",   PolarRSub))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, "*=>",   PolarRMul))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, "/=>",  PolarRDiv))
  return true;
}

INSTR(VecMember) {
  if(instr->m_val)
    *(m_float**)REG(-SZ_INT) = (m_float*)(*(m_bit**)REG(-SZ_INT) + instr->m_val2);
  else {
    *(m_float*)REG(-SZ_INT) = *(m_float*)(*(m_bit**)REG(-SZ_INT) + instr->m_val2);
    PUSH_REG(shred, SZ_FLOAT - SZ_INT);
  }
}

#define describe_vec(func, op)                                                \
ANN static void vec_##func(const m_bit* v, const int size, const m_float f) { \
  for(int i = size + 1; --i;)                                                 \
    *(m_float*)(v + (i-1) * SZ_FLOAT) op##= f;                                \
}
describe_vec(Add, +)
describe_vec(Sub, -)
describe_vec(Mul, *)
describe_vec(Div, /)

static MFUN(vec3_set) {
  m_vec3* v =  *(m_vec3**)MEM(0);
  v->x = *(m_float*)MEM(SZ_INT);
  v->y = *(m_float*)MEM(SZ_INT + SZ_FLOAT);
  v->z = *(m_float*)MEM(SZ_INT + SZ_FLOAT * 2);
  *(m_vec3*)RETURN = *v;
}

static MFUN(vec3_setAll) {
  m_vec3* v =  *(m_vec3**)MEM(0);
  v->x = *(m_float*)MEM(SZ_INT);
  v->y = *(m_float*)MEM(SZ_INT);
  v->z = *(m_float*)MEM(SZ_INT);
}

static MFUN(vec3_magnitude) {
  const m_vec3* v =  *(m_vec3**)MEM(0);
  *(m_float*)RETURN = sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
}

static MFUN(vec3_normalize) {
  const m_vec3* v =  *(m_vec3**)MEM(0);
  const m_float mag = sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
  if(mag  > 0)
    vec_Div((m_bit*)v, 3, mag);
}

#define describe_vec3_x(name, arg)    \
static MFUN(vec3_##name) {            \
  m_vec3* v =  *(m_vec3**)MEM(0);   \
  v->x = (v->y - v->x) * v->z arg;    \
  *(m_float*)RETURN = v->x;           \
}
describe_vec3_x(interp, + v->x)
describe_vec3_x(float, * v->z * (*(m_float*)MEM(SZ_INT)) + v->x)
describe_vec3_x(dur, * (*(m_float*)MEM(SZ_INT) / (m_float)shred->info->vm->bbq->si->sr) + v->x)

static MFUN(vec3_update) {
  m_vec3* v =  *(m_vec3**)MEM(0);
  v->y = *(m_float*)MEM(SZ_INT);
}

static MFUN(vec3_update_slew) {
  m_vec3* v =  *(m_vec3**)MEM(0);
  v->y = *(m_float*)MEM(SZ_INT);
  v->z = *(m_float*)MEM(SZ_INT + SZ_FLOAT);
}

static MFUN(vec3_update_set) {
  m_vec3* v =  *(m_vec3**)MEM(0);
  v->x = *(m_float*)MEM(SZ_INT);
  v->y = *(m_float*)MEM(SZ_INT);

}

static MFUN(vec3_update_set_slew) {
  m_vec3* v =  *(m_vec3**)MEM(0);
  v->x = *(m_float*)MEM(SZ_INT);
  v->y = *(m_float*)MEM(SZ_INT);
  v->z = *(m_float*)MEM(SZ_INT + SZ_FLOAT);
}

#define describe_vec3(name, op)           \
static INSTR(Vec3##name) {\
  POP_REG(shred, SZ_VEC3);       \
  m_vec3 r, * t = (m_vec3*)REG(-SZ_VEC3); \
  r.x = t->x op (t + 1)->x;               \
  r.y = t->y op (t + 1)->y;               \
  r.z = t->z op (t + 1)->z;               \
  *(m_vec3*)REG(-SZ_VEC3) = r;            \
}
describe_vec3(Add, +)
describe_vec3(Sub, -)
describe_vec3(Mul, *)
describe_vec3(Div, /)


#define describe_float_vec3(func)               \
static INSTR(Float##func##Vec3) {\
  POP_REG(shred, SZ_FLOAT);                     \
  const m_float f = *(m_float*)REG(-SZ_VEC3);   \
  const m_vec3 r = *(m_vec3*)REG(-SZ_COMPLEX);  \
  vec_Mul((m_bit*)&r, 3, f);                    \
  *(m_vec3*)REG(-SZ_VEC3) = r;                  \
}
describe_float_vec3(Add)
describe_float_vec3(Sub)
describe_float_vec3(Mul)
describe_float_vec3(Div)

#define describe_vec3_float(func)               \
static INSTR(Vec3##func##Float) {\
  POP_REG(shred, SZ_FLOAT);                     \
  const m_vec3 r = *(m_vec3*)REG(-SZ_VEC3);     \
  const m_float f = *(m_float*)REG(0);          \
  vec_##func((m_bit*)&r, 3, f);                 \
  *(m_vec3*)REG(-SZ_VEC3) = r;                  \
}
describe_vec3_float(Add)
describe_vec3_float(Sub)
describe_vec3_float(Mul)
describe_vec3_float(Div)

static INSTR(Vec3RAssign) {
  POP_REG(shred, SZ_INT);
  m_vec3* r = *(m_vec3**)REG(0);
  r->x = *(m_float*)REG(-SZ_VEC3);
  r->y = *(m_float*)REG(-SZ_COMPLEX);
  r->z = *(m_float*)REG(-SZ_FLOAT);
  *(m_vec3*)REG(0) = *r;
}

static void vecx_base(const Gwi gwi) {
  gwi_item_ini(gwi, "float", "x");
  gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "float", "y");
  gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "float", "z");
  gwi_item_end(gwi, ae_flag_none, num, 0);
}

static GACK(gack_vec3) {
  m_float *ptr = (m_float*)VALUE;
  INTERP_PRINTF("Vec3(%.4f, %.4f, %.4f)", *ptr, *(ptr + 1), *(ptr+2));
}

EQUALITY_OPER(vec3, SZ_VEC3);

OP_CHECK(opck_vecx_ctor) {
  Exp_Call *call = (Exp_Call*)data;
  Exp* e = call->args;
  Exp *last = NULL;
  if(call->args)
    CHECK_ON(check_exp(env, call->args));
  size_t i = 0;
  const Type t_float = env->gwion->type[et_float];
  while(e) {
    CHECK_ON(check_implicit(env, e, t_float));
    i += SZ_FLOAT;
    last = e;
    e = e->next;
  }
//  const Type t = _class_base(call->func->type);
  const Type t = call->func->type;
printf("t %p\n", call->func->type);
//exit(3);
  if(i > t->size) {
    env_err(env, last->loc, "extraneous component of %s value", t->name);
    return NULL;
  }
  if(!call->args) {
    call->args = last = new_prim_float(env->gwion->mp, 0.0, call->func->loc);
    last->type = t_float;
    i += SZ_FLOAT;
  }
  while(i < t->size) {
    last = (last->next = new_prim_float(env->gwion->mp, 0.0, last->loc));
    last->type = t_float;
    i += SZ_FLOAT;
  }
exp_self(call)->type = t;
  return NULL;
//  return t;
}

static GWION_IMPORT(vec3) {
  const Type t_vec3 = gwi_struct_ini(gwi, "Vec3");
  GWI_B(gwi_gack(gwi, t_vec3, gack_vec3))
  vecx_base(gwi);
  gwi_func_ini(gwi, "void", "set");
  gwi_func_arg(gwi, "float", "x");
  gwi_func_arg(gwi, "float", "y");
  gwi_func_arg(gwi, "float", "z");
  GWI_B(gwi_func_end(gwi, vec3_set, ae_flag_none))
    gwi_func_ini(gwi, "void", "setAll");
  gwi_func_arg(gwi, "float", "x");
  GWI_B(gwi_func_end(gwi, vec3_setAll, ae_flag_none))
    gwi_func_ini(gwi, "float", "magnitude");
  GWI_B(gwi_func_end(gwi, vec3_magnitude, ae_flag_none))
    gwi_func_ini(gwi, "void", "normalize");
  GWI_B(gwi_func_end(gwi, vec3_normalize, ae_flag_none))
    gwi_func_ini(gwi, "float", "interp");
  GWI_B(gwi_func_end(gwi, vec3_interp, ae_flag_none))
    gwi_func_ini(gwi, "float", "interp");
  gwi_func_arg(gwi, "float", "delta");
  GWI_B(gwi_func_end(gwi, vec3_float, ae_flag_none))
    gwi_func_ini(gwi, "float", "interp");
  gwi_func_arg(gwi, "dur", "delta");
  GWI_B(gwi_func_end(gwi, vec3_dur, ae_flag_none))
    gwi_func_ini(gwi, "void", "update");
  gwi_func_arg(gwi, "float", "goal");
  GWI_B(gwi_func_end(gwi, vec3_update, ae_flag_none))
    gwi_func_ini(gwi, "void", "update");
  gwi_func_arg(gwi, "float", "goal");
  gwi_func_arg(gwi, "float", "slew");
  GWI_B(gwi_func_end(gwi, vec3_update_slew, ae_flag_none))
    gwi_func_ini(gwi, "void", "supdate");
  gwi_func_arg(gwi, "float", "goalAndValue");
  GWI_B(gwi_func_end(gwi, vec3_update_set, ae_flag_none))
    gwi_func_ini(gwi, "void", "supdate");
  gwi_func_arg(gwi, "float", "goalAndValue");
  gwi_func_arg(gwi, "float", "slew");
  GWI_B(gwi_func_end(gwi, vec3_update_set_slew, ae_flag_none))

  gwi_func_ini(gwi, "auto", "new");
  gwi_func_arg(gwi, "float", "x");
  gwi_func_arg(gwi, "float", "y");
  gwi_func_arg(gwi, "float", "z");
  GWI_B(gwi_func_end(gwi, vec3_set, ae_flag_none))

  GWI_B(gwi_class_end(gwi))

   GWI_B(gwi_oper_ini(gwi, "Vec3", "Vec3", "bool"))
   GWI_B(gwi_oper_end(gwi, "==",          vec3_eq))
   GWI_B(gwi_oper_end(gwi, "!=",          vec3_ne))
   GWI_B(gwi_oper_ini(gwi, "Vec3", "Vec3", "Vec3"))
   GWI_B(gwi_oper_end(gwi, "+", Vec3Add))
   GWI_B(gwi_oper_end(gwi, "-", Vec3Sub))
   GWI_B(gwi_oper_end(gwi, "*", Vec3Mul))
   GWI_B(gwi_oper_end(gwi, "/", Vec3Div))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, ":=>", Vec3RAssign))
   GWI_B(gwi_oper_ini(gwi, "Vec3", "float", "Vec3"))
   GWI_B(gwi_oper_end(gwi, "+",  Vec3AddFloat))
   GWI_B(gwi_oper_end(gwi, "-", Vec3SubFloat))
   GWI_B(gwi_oper_end(gwi, "*",  Vec3MulFloat))
   GWI_B(gwi_oper_end(gwi, "/", Vec3DivFloat))
   GWI_B(gwi_oper_ini(gwi, "float", "Vec3", "Vec3"))
   GWI_B(gwi_oper_end(gwi, "+",  FloatAddVec3))
   GWI_B(gwi_oper_end(gwi, "-",  FloatSubVec3))
   GWI_B(gwi_oper_end(gwi, "*",  FloatMulVec3))
   GWI_B(gwi_oper_end(gwi, "/",  FloatDivVec3))
  return true;
}

static MFUN(vec4_set) {
  m_vec4* v =  *(m_vec4**)MEM(0);
  v->x = *(m_float*)MEM(SZ_INT);
  v->y = *(m_float*)MEM(SZ_INT + SZ_FLOAT);
  v->z = *(m_float*)MEM(SZ_INT + SZ_FLOAT * 2);
  v->w = *(m_float*)MEM(SZ_INT + SZ_FLOAT * 3);
}

static MFUN(vec4_setAll) {
  m_vec4* v =  *(m_vec4**)MEM(0);
  v->x = *(m_float*)MEM(SZ_INT);
  v->y = *(m_float*)MEM(SZ_INT);
  v->z = *(m_float*)MEM(SZ_INT);
  v->w = *(m_float*)MEM(SZ_INT);
}

static MFUN(vec4_magnitude) {
  m_vec4* v =  *(m_vec4**)MEM(0);
  *(m_float*)RETURN = sqrt(v->x * v->x + v->y * v->y + v->z * v->z + v->w * v->w);
}

static MFUN(vec4_normalize) {
  m_vec4* v =  *(m_vec4**)MEM(0);
  const m_float mag = sqrt(v->x * v->x + v->y * v->y + v->z * v->z + v->w * v->w);
  if(mag  > 0)
    vec_Div((m_bit*)v, 4, mag);
}

#define describe_vec4(name, op)           \
static INSTR(Vec4##name) {                \
  POP_REG(shred, SZ_VEC4);                \
  m_vec4 r, * t = (m_vec4*)REG(-SZ_VEC4); \
  r.x = t->x op (t + 1)->x;               \
  r.y = t->y op (t + 1)->y;               \
  r.z = t->z op (t + 1)->z;               \
  r.w = t->w op (t + 1)->w;               \
  *(m_vec4*)REG(-SZ_VEC4) = r;            \
}
describe_vec4(Add, +)
describe_vec4(Sub, -)
describe_vec4(Mul, *)
describe_vec4(Div, /)

#define describe_float_vec4(func)               \
static INSTR(Float##func##Vec4) {\
  POP_REG(shred, SZ_FLOAT);                     \
  m_float f = *(m_float*)REG(-SZ_VEC4);         \
  m_vec4 r = *(m_vec4*)REG(-SZ_VEC3);           \
  vec_##func((m_bit*)&r, 4, f);                 \
  *(m_vec4*)REG(-SZ_VEC4) = r;                  \
}
describe_float_vec4(Add)
describe_float_vec4(Sub)
describe_float_vec4(Mul)
describe_float_vec4(Div)

#define describe_vec4_float(func)               \
static INSTR(Vec4##func##Float) {\
  POP_REG(shred, SZ_FLOAT);                     \
  m_vec4 r = *(m_vec4*)REG(-SZ_VEC4);           \
  m_float f = *(m_float*)REG(0);                \
  vec_##func((m_bit*)&r, 4, f);                 \
  *(m_vec4*)REG(-SZ_VEC4) = r;                  \
}
describe_vec4_float(Add)
describe_vec4_float(Sub)
describe_vec4_float(Mul)
describe_vec4_float(Div)

static INSTR(Vec4RAssign) {
  POP_REG(shred, SZ_INT);
  m_vec4* r = *(m_vec4**)REG(0);
  r->x = *(m_float*)REG(-SZ_VEC4);
  r->y = *(m_float*)REG(-SZ_VEC3);
  r->z = *(m_float*)REG(-SZ_COMPLEX);
  r->w = *(m_float*)REG(-SZ_FLOAT);
  *(m_vec4*)REG(0) = *r;
}

static GACK(gack_vec4) {
  INTERP_PRINTF("Vec4(%.4f, %.4f, %.4f, %.4f)",
      *(m_float*)VALUE,
      *(m_float*)(VALUE + SZ_FLOAT),
      *(m_float*)(VALUE + SZ_FLOAT*2),
      *(m_float*)(VALUE + SZ_FLOAT*3));
}

EQUALITY_OPER(vec4, SZ_VEC4);

static GWION_IMPORT(vec4) {
  const Type t_vec4 = gwi_struct_ini(gwi, "Vec4");
  GWI_B(gwi_gack(gwi, t_vec4, gack_vec4))
  vecx_base(gwi);
  gwi_item_ini(gwi, "float", "w");
  gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_func_ini(gwi, "void", "set");
  gwi_func_arg(gwi, "float", "x");
  gwi_func_arg(gwi, "float", "y");
  gwi_func_arg(gwi, "float", "z");
  gwi_func_arg(gwi, "float", "w");
  GWI_B(gwi_func_end(gwi, vec4_set, ae_flag_none))
    gwi_func_ini(gwi, "void", "setAll");
  gwi_func_arg(gwi, "float", "x");
  GWI_B(gwi_func_end(gwi, vec4_setAll, ae_flag_none))
    gwi_func_ini(gwi, "float", "magnitude");
  GWI_B(gwi_func_end(gwi, vec4_magnitude, ae_flag_none))
    gwi_func_ini(gwi, "void", "normalize");
  GWI_B(gwi_func_end(gwi, vec4_normalize, ae_flag_none))

  gwi_func_ini(gwi, "auto", "new");
  gwi_func_arg(gwi, "float", "x");
  gwi_func_arg(gwi, "float", "y");
  gwi_func_arg(gwi, "float", "z");
  gwi_func_arg(gwi, "float", "w");
  GWI_B(gwi_func_end(gwi, vec4_set, ae_flag_none))
  GWI_B(gwi_struct_end(gwi))

   GWI_B(gwi_oper_ini(gwi, "Vec4", "Vec4", "bool"))
   GWI_B(gwi_oper_end(gwi, "==",          vec4_eq))
   GWI_B(gwi_oper_end(gwi, "!=",          vec4_ne))
   GWI_B(gwi_oper_ini(gwi, "Vec4", "Vec4", "Vec4"))
   GWI_B(gwi_oper_end(gwi, "+",  Vec4Add))
   GWI_B(gwi_oper_end(gwi, "-", Vec4Sub))
   GWI_B(gwi_oper_end(gwi, "*", Vec4Mul))
   GWI_B(gwi_oper_end(gwi, "/", Vec4Div))
   GWI_B(gwi_oper_add(gwi, opck_rassign))
   GWI_B(gwi_oper_end(gwi, ":=>", Vec4RAssign))
   GWI_B(gwi_oper_ini(gwi, "Vec4", "float", "Vec4"))
   GWI_B(gwi_oper_end(gwi, "+", Vec4AddFloat))
   GWI_B(gwi_oper_end(gwi, "-", Vec4SubFloat))
   GWI_B(gwi_oper_end(gwi, "*", Vec4MulFloat))
   GWI_B(gwi_oper_end(gwi, "/", Vec4DivFloat))
   GWI_B(gwi_oper_ini(gwi, "float", "Vec4", "Vec4"))
   GWI_B(gwi_oper_end(gwi, "+", FloatAddVec4))
   GWI_B(gwi_oper_end(gwi, "-", FloatSubVec4))
   GWI_B(gwi_oper_end(gwi, "*", FloatMulVec4))
  return gwi_oper_end(gwi, "/", FloatDivVec4);
}

GWION_IMPORT(Vecx) {
  GWI_B(gwimport_complex(gwi))
  GWI_B(gwimport_vec3(gwi))
  GWI_B(gwimport_vec4(gwi))
  return true;
}
