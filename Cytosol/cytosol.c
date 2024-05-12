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
#include "traverse.h"
#include "emit.h"
#include "gwi.h"
#include "parse.h"
#include "cytosol.h"

#define PROG(o)      (*(struct cyt_program**)o->data)
#define RUNNER(o)    (*(struct cyt_driver_runner**)(o->data + SZ_INT))
#define CELLENV(o)   (*(struct cyt_cell_env**)(o->data + SZ_INT*2))
#define EXECSTATE(o) (*(struct cyt_exec_state**)(o->data + SZ_INT*3))
#define FUNCVEC(o)   (*(struct Vector_*)(o->data + SZ_INT*4))

#define BUFFER(o)     (*(struct cyt_value_buffer**)o->data)
#define INDEX(o)      (*(m_uint*)(o->data + SZ_INT))
#define BORROWED(o)   (*(m_int*)(o->data + SZ_INT*2))

struct CytosolClosure_ {
  VM_Shred shred;
  VM_Code  code;
  struct Vector_ args;
};

static CTOR(cytosol_ctor) {
  PROG(o) = cyt_program_new();
  RUNNER(o) = cyt_driver_runner_new();
  CELLENV(o) = cyt_cellenv_new();
  EXECSTATE(o) = cyt_exec_state_new();
  vector_init(&FUNCVEC(o));
}

static DTOR(cytosol_dtor) {
  cyt_program_destroy(PROG(o));
  cyt_driver_runner_destroy(RUNNER(o));
  cyt_cellenv_destroy(CELLENV(o));
  cyt_exec_state_destroy(EXECSTATE(o));

  for(m_uint i = 0; i < vector_size(&FUNCVEC(o)); i++) {
    struct CytosolClosure_ *closure = (struct CytosolClosure_*)vector_at(&FUNCVEC(o), i);
    _release(closure->shred->info->me, shred);
    vmcode_remref(closure->code, shred->info->vm->gwion);
    vector_release(&closure->args);
    mp_free(shred->info->mp, CytosolClosure, closure);
  }
  vector_release(&FUNCVEC(o));
}

static MFUN(file_from_string) {
  cyt_driver_runner_add_file_from_string(RUNNER(o),
    STRING(*(M_Object*)MEM(SZ_INT)),
    STRING(*(M_Object*)MEM(SZ_INT*2)));
}

static MFUN(file_from_path) {
  cyt_driver_runner_add_file_from_path(RUNNER(o),
    STRING(*(M_Object*)MEM(SZ_INT)));
}

static MFUN(cytosol_compile) {
  *(m_uint*)RETURN = cyt_driver_runner_compile(RUNNER(o), PROG(o));
}

static cyt_value_buffer* cytosol_buffer(Type *types, const M_Object o) {
  const Vector v = &o->type_ref->info->tuple->contains;
  const m_uint sz = vector_size(v);
  cyt_value_buffer *buf = cyt_value_buffer_new(sz);
  for(m_uint i = 0; i < sz; i++) {
    const Type t = (Type)vector_at(v, i);
    if(isa(t, types[et_int]))
      cyt_value_buffer_set_int(buf, i, *(m_int*)(o->data + i*SZ_INT));
    else if(isa(t, types[et_string]))
      cyt_value_buffer_set_string(buf, i, STRING(*(M_Object*)(o->data + i*SZ_INT)));
    else if(isa(t, types[et_bool]))
      cyt_value_buffer_set_bool(buf, i, *(m_uint*)(o->data + i*SZ_INT));
    else {
      cyt_value_buffer *_buf = cytosol_buffer(types, *(M_Object*)(o->data + i*SZ_INT));
      cyt_value_buffer_set_record(buf, i, _buf);
    }
  }
  return buf;
}

static MFUN(cytosol_add_record) {
  const m_int quantity = *(m_int*)MEM(SZ_INT);
  const M_Object record = *(M_Object*)MEM(SZ_INT*2);
  struct cyt_record_id out_id = {};
  if(!cyt_program_record_by_name(PROG(o), record->type_ref->name, &out_id)) {
    xfun_handle(shred, "Invalid record Creation");
    return;
  }
  cyt_value_buffer *buf = cytosol_buffer(shred->info->vm->gwion->type, record);
  cyt_cellenv_add_record(CELLENV(o), quantity, out_id, buf);
}

static MFUN(cytosol_run) {
  const m_int bound = *(m_int*)MEM(SZ_INT);
  if(bound < 0) {
    xfun_handle(shred, "Negative iteration requested");
    return;
  }
  cyt_driver_runner_run(
    RUNNER(o), PROG(o), EXECSTATE(o), CELLENV(o), bound);
}

static MFUN(cytosol_run_single) {
  *(m_int*)RETURN = !cyt_driver_runner_run_single_iteration(
    RUNNER(o), PROG(o), EXECSTATE(o), CELLENV(o));
}

static MFUN(cytosol_count) {
  struct cyt_record_id out_id;
  const M_Object name = *(M_Object*)MEM(SZ_INT);
  if(cyt_program_record_by_name(PROG(o), STRING(name), &out_id)) {
    *(m_int*)RETURN = cyt_cellenv_count_records(CELLENV(o), out_id);
  } else
    *(m_int*)RETURN = 0;
}

struct CytosolArg {
  m_bit *const data;
  VM_Shred shred;
  struct Vector_ types;
  const struct cyt_value_buffer* buf;
};

ANN static bool cytosol_args(const Gwion gwion, struct CytosolArg *const ca) {
  const Vector v = &ca->types;
  for(m_uint i = 0; i < vector_size(v); i++) {
    const Type t = (Type)vector_at(v, i);
    m_bit *data = ca->data + i*SZ_INT;
    if(isa(t, gwion->type[et_int])) {
      ptrdiff_t n;
      cyt_value_buffer_get_int(ca->buf, i, &n);
      *(m_int*)(ca->data + i*SZ_INT) = n;
    } else if(isa(t, gwion->type[et_bool])) {
      bool b;
      cyt_value_buffer_get_bool(ca->buf, i, &b);
      *(m_uint*)(ca->data + i*SZ_INT) = b;
    } else if(isa(t, gwion->type[et_string])) {
      const char* data;
      size_t outlen;
      cyt_value_buffer_get_string(ca->buf, i, &data, &outlen);
      char c[outlen + 1];
      strncpy(c, data, outlen);
      c[outlen] = '\0';
      const M_Object str = new_string(gwion, c);
      *(M_Object*)(ca->data + i*SZ_INT) = str;
    } else {
return false;
/*
      const M_Object o = new_object(gwion->mp, t);
      *(M_Object*)(ca->data + i*SZ_INT) = o;
      struct Vector_ tmp;
      const Vector v = &t->info->tuple->contains;
      vector_init(&tmp);
      for(m_uint j = 0; j < vector_size(v); j++) {
        const Type t = (Type)vector_at(v, j);
        vector_add(&tmp, (m_uint)t);
      }
      cyt_value_buffer *out;
      cyt_value_buffer_get_record_fields(ca->buf, i, &out);
      struct CytosolArg record = {
        .shred = ca->shred,
        .data  = o->data,
        .types = tmp,
        .buf = out
      };
      cytosol_args(gwion, &record);
      vector_release(&tmp);
*/
    }
  }
  return true;
}

static void cytosol_fun(void* data, const struct cyt_value_buffer* buf) {
  struct CytosolClosure_ *closure = (struct CytosolClosure_*)data;
  vmcode_addref(closure->code);
  const VM_Shred shred = new_vm_shred(closure->shred->info->mp, closure->code);
  shred->base = closure->shred->base;
  struct CytosolArg args = {
    .shred = shred,
    .data  = shred->mem,
    .types = closure->args,
    .buf = buf
  };
  if(cytosol_args(closure->shred->info->vm->gwion, &args))
    vm_add_shred(closure->shred->info->vm, shred);
}

static void cytosol_cb(const M_Object o, const VM_Shred shred, const VM_Code code, const struct Vector_ types, const m_str name) {
  struct CytosolClosure_ *closure = mp_malloc(shred->info->mp, CytosolClosure);
  closure->shred = shred;
  closure->code = vmcode_callback(shred->info->mp, code);
  closure->args = types;
  ++closure->shred->info->me->ref;
  vector_add(&FUNCVEC(o), (m_uint)closure);
  cyt_exec_state_set_extern_function(EXECSTATE(o),
    name, cytosol_fun, closure);
}

static INSTR(func2cyt) {
  const M_Object o = *(M_Object*)REG(-SZ_INT);
  const VM_Code code = *(VM_Code*)REG(0);
  char *end = strchr(code->name, '@');
  const ptrdiff_t diff = end - code->name;
  char c[diff + 1];
  strncpy(c, code->name, diff);
  c[diff] = 0;
  cytosol_cb(o, shred, code, *((struct Vector_*)&instr->m_val), c);
}

// C defined func won't work atm
static OP_CHECK(opck_func2cyt) {
  const Exp_Binary *bin = (Exp_Binary*)data;
  const Func func = bin->rhs->type->info->func;
  const Type fields = (Type)vector_front(&bin->lhs->type->info->tuple->contains);
  Arg_List args = func->def->base->args;
  if(args) {
    for(uint32_t i = 0; i < args->len; i++) {
      Arg *arg = mp_vector_at(args, Arg, i);
      if(!isa(arg->type, env->gwion->type[et_int]) &&
         !isa(arg->type, env->gwion->type[et_string]) &&
         arg->type == fields && !isa(arg->type, fields))
        ERR_N(arg->var_decl.tag.loc, "invalid type in field assignment");
    }
  }
  return bin->lhs->type;
}

static OP_EMIT(opem_func2cyt) {
  const Exp_Binary *bin = (Exp_Binary*)data;
  const Func func = bin->rhs->type->info->func;
  emit_regmove(emit, -SZ_INT);
  const Instr instr = emit_add_instr(emit, func2cyt);
  struct Vector_ v;
  vector_init(&v);
  Arg_List args = func->def->base->args;
  if(args) {
    for(uint32_t i = 0; i < args->len; i++) {
      Arg *arg = mp_vector_at(args, Arg, i);
      vector_add(&v, (m_uint)arg->type);
    }
  }
  instr->m_val = (m_uint)v.ptr;
  return true;
}

static m_int cytosol_stmt_list(const Env env, const Type fields, Stmt_List list) {
  for(uint32_t i = 0; i < list->len; i++) {
    Stmt* stmt = mp_vector_at(list, Stmt, i);
    if (stmt->stmt_type == ae_stmt_exp &&
        stmt->d.stmt_exp.val->exp_type == ae_exp_decl &&
        stmt->d.stmt_exp.val->d.exp_decl.list->len == 1) {
      const Type type = mp_vector_at(stmt->d.stmt_exp.val->d.exp_decl.list, struct Var_Decl_, 0)->value->type;
      if(!isa(type, env->gwion->type[et_int]) &&
        !isa(type, env->gwion->type[et_string]) &&
        !isa(type, fields))
      ERR_B(stmt->d.stmt_exp.val->loc, "invalid type '%s' in Cytosol.field", type->name)
   } else
      ERR_B(stmt->loc, "invalid stmt in Cytosol.field")
  }
  return list->len;
}

static OP_CHECK(opck_record_ctor) {
  Exp_Call *call = (Exp_Call*)data;
  Exp* arg = call->args;
  CHECK_NN(check_exp(env, arg));
  const Vector v = &actual_type(env->gwion, call->func->type)->info->tuple->contains;
  m_uint i = 0;
  while(arg) {
    const Type t = (Type)vector_at(v, i);
    if(!isa(arg->type, t))
       ERR_N(arg->loc, _("invalid type '%s' in '%s' constructor (expected '%s')"),
            arg->type->name, call->func->type->name, t->name);
    i++;
    arg = arg->next;
  }
  return actual_type(env->gwion, call->func->type);
}

static INSTR(RecordCtor) {
  POP_REG(shred, instr->m_val);
  const M_Object o = new_object(shred->info->mp, (Type)instr->m_val2);
  memcpy(o->data, REG(-SZ_INT), instr->m_val);
  *(M_Object*)REG(-SZ_INT) = o;
}

static OP_EMIT(opem_record_ctor) {
  Exp_Call *call = (Exp_Call*)data;
  const Type t = actual_type(emit->gwion, call->func->type);
  const Vector v = &t->info->tuple->contains;
  const size_t sz = vector_size(v);
  for(m_uint i = 0; i < sz; i++) {
    const Type t = (Type)vector_at(v, i);
    if(tflag(t, tflag_release))
      emit_compound_addref(emit, t, (-sz + i - 1) * SZ_INT, false);
  }
  const Instr instr = emit_add_instr(emit, RecordCtor);
  instr->m_val = sz * SZ_INT;
  instr->m_val2 = (m_uint)t;
  return true;
}

GWION_IMPORT(Cytosol) {
  DECL_B(const Type, t_cytosol, = gwi_class_ini(gwi, "Cytosol", "Object"));
  gwi_class_xtor(gwi, cytosol_ctor, cytosol_dtor);

    DECL_B(const Type, t_fields, = gwi_class_ini(gwi, "Record", "Object"));
    SET_FLAG(t_fields, abstract);
    GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_enum_ini(gwi, "Type"))
  GWI_B(gwi_enum_add(gwi, "int_t", 0))
  GWI_B(gwi_enum_add(gwi, "string_t", 0))
  GWI_B(gwi_enum_add(gwi, "record_t", 0))
  GWI_B(gwi_enum_end(gwi))
  t_fields->nspc->offset += SZ_INT*5;

  GWI_B(gwi_func_ini(gwi, "void", "add_string"))
  GWI_B(gwi_func_arg(gwi, "string", "name"))
  GWI_B(gwi_func_arg(gwi, "string", "source"))
  GWI_B(gwi_func_end(gwi, file_from_string, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "void", "add_path"))
  GWI_B(gwi_func_arg(gwi, "string", "path"))
  GWI_B(gwi_func_end(gwi, file_from_path, ae_flag_none))

  // add enum?
  GWI_B(gwi_func_ini(gwi, "void", "run"))
  GWI_B(gwi_func_arg(gwi, "int", "bound"))
  GWI_B(gwi_func_end(gwi, cytosol_run, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "bool", "run_single"))
  GWI_B(gwi_func_end(gwi, cytosol_run_single, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "void", "add_record"))
  GWI_B(gwi_func_arg(gwi, "int", "quantity"))
  GWI_B(gwi_func_arg(gwi, "Record", "fields"))
  GWI_B(gwi_func_end(gwi, cytosol_add_record, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "bool", "compile"))
  GWI_B(gwi_func_end(gwi, cytosol_compile, ae_flag_none))

  GWI_B(gwi_func_ini(gwi, "int", "count"))
  GWI_B(gwi_func_arg(gwi, "string", "name"))
  GWI_B(gwi_func_end(gwi, cytosol_count, ae_flag_none))

  GWI_B(gwi_class_end(gwi))

  // define operators at global scope

   GWI_B(gwi_oper_ini(gwi, "Cytosol", "Function", "Cytosol"))
   GWI_B(gwi_oper_add(gwi, opck_func2cyt))
   GWI_B(gwi_oper_emi(gwi, opem_func2cyt))
   GWI_B(gwi_oper_end(gwi, "<<", NULL))

   GWI_B(gwi_oper_ini(gwi, NULL, "Cytosol.Record", NULL))
   GWI_B(gwi_oper_add(gwi, opck_record_ctor))
   GWI_B(gwi_oper_emi(gwi, opem_record_ctor))
   GWI_B(gwi_oper_end(gwi, "call_type", NULL))

  return true;
}
