#include <stdlib.h>
#include <string.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "shreduler_private.h"
#include "instr.h"
#include "gwion.h"
#include "object.h"
#include "plug.h"
#include "operator.h"
#include "import.h"
#include "compile.h"
#include "traverse.h"
#include "emit.h"
#include "array.h"
#include "pass.h"
#include "gwi.h"

static SFUN(machine_add) {
  const M_Object obj = *(M_Object*)MEM(0);
  const m_str str = STRING(obj);
  const m_uint ret = compile_filename(shred->info->vm->gwion, str);
  if(ret > 0) *(m_int*)RETURN = ret;
  else xfun_handle(shred, "InvalidRuntimeCompilation");
}

static ValueList *get_values(const VM_Shred shred, const M_Vector names) {
  const m_uint code_offset = ((Instr)vector_at(&shred->code->instr, shred->pc-3))->m_val;
  const VM_Code code = *(VM_Code*)REG(-code_offset - SZ_INT);
  const uint32_t clen = code->types->len;
  const uint32_t nlen = ARRAY_LEN(names);
  const uint32_t len = clen <= nlen ? clen : nlen;
  ValueList *const values = new_valuelist(shred->info->mp, code->types->len);
  m_uint offset = SZ_INT*2;
  for(uint32_t i = 0; i < len; i++) {
    const Type t = typelist_at(code->types, i);
    const M_Object o = *(M_Object*)(names->ptr + ARRAY_OFFSET + i * SZ_INT);
    m_bit *data = MEM(offset);
    anytype_addref(t, data);
    const Value v = new_value(shred->info->vm->gwion->env, t, MK_TAG(insert_symbol(shred->info->vm->gwion->st, STRING(o)), t->info->value->from->loc));
    if(t->size <= SZ_INT)
      v->d.ptr = *(void**)data;
    else {
      v->d.ptr = mp_calloc2(shred->info->mp, t->size);
      memcpy(v->d.ptr, data, t->size);
    }
    valuelist_set(values, i, v);
    offset += t->size;
  }
  return values;
}

static SFUN(machine_add_values) {
  const M_Object obj = *(M_Object*)MEM(0);
  const m_str filename = STRING(obj);
  const M_Vector names = ARRAY(*(M_Object*)MEM(SZ_INT));
  ValueList *const values = get_values(shred, names);
  const m_uint ret = compile_filename_values(shred->info->vm->gwion, filename, values);
  free_valuelist(shred->info->mp, values);
  if(ret > 0) *(m_int*)RETURN = ret;
  else xfun_handle(shred, "InvalidRuntimeCompilation");
}

static SFUN(machine_remove) {
  const VM* vm = shred->info->vm;
  const m_int xid = *(m_int*)MEM(0);
  if(!vm_remove(vm, xid))
    xfun_handle(shred, "InvalidShredRemove");
}

static SFUN(machine_replace) {
  const VM* vm = shred->info->vm;
  const m_int xid = *(m_int*)MEM(0);
  if(!vm_remove(vm, xid))
    xfun_handle(shred, "InvalidShredRemove");
  const M_Object str = *(M_Object*)MEM(SZ_INT);
  if(!compile_filename_xid(vm->gwion, STRING(str), xid))
    xfun_handle(shred, "InvalidRuntimeCompilation");
}

static SFUN(machine_replace_values) {
  const VM* vm = shred->info->vm;
  const m_int xid = *(m_int*)MEM(0);
  if(!vm_remove(vm, xid))
    xfun_handle(shred, "InvalidShredRemove");
  const M_Object str = *(M_Object*)MEM(SZ_INT);
  const M_Vector names = ARRAY(*(M_Object*)MEM(SZ_INT*2));
  ValueList *const values = get_values(shred, names);
  const bool ret = compile_filename_xid_values(vm->gwion, STRING(str), xid, values);
  free_valuelist(shred->info->mp, values);
  if(ret) *(m_int*)RETURN = ret;
  else xfun_handle(shred, "InvalidRuntimeCompilation");
}

static SFUN(machine_check) {
  const M_Object code_obj = *(M_Object*)MEM(0);
  const m_str line = STRING(code_obj);
  struct Vector_ v;
  Vector back = vector_copy(shred->info->mp, &shred->info->vm->gwion->data->passes->vec);
  vector_init(&v);
  vector_add(&v, (vtype)"check");
  pass_set(shred->info->vm->gwion, &v);
  vector_release(&v);
  const m_uint ret = compile_string(shred->info->vm->gwion, "Machine.check", line);
  pass_set(shred->info->vm->gwion, back);
  free_vector(shred->info->mp, back);
  if(ret > 0) *(m_int*)RETURN = ret;
  else xfun_handle(shred, "InvalidRuntimeCompilation");
}

static SFUN(machine_check_values) {
  const M_Object code_obj = *(M_Object*)MEM(0);
  const m_str line = STRING(code_obj);
  struct Vector_ v;
  struct Vector;
  Vector back = vector_copy(shred->info->mp, &shred->info->vm->gwion->data->passes->vec);
  vector_init(&v);
  vector_add(&v, (vtype)"check");
  pass_set(shred->info->vm->gwion, &v);
  vector_release(&v);
  const M_Vector names = ARRAY(*(M_Object*)MEM(SZ_INT));
  ValueList *const values = get_values(shred, names);
  const m_uint ret = compile_string_values(shred->info->vm->gwion, "Machine.check", line, values);
  free_valuelist(shred->info->mp, values);
  pass_set(shred->info->vm->gwion, back);
  free_vector(shred->info->mp, back);
  if(ret > 0) *(m_int*)RETURN = ret;
  else xfun_handle(shred, "InvalidRuntimeCompilation");
}

static SFUN(machine_compile) {
  const M_Object code_obj = *(M_Object*)MEM(0);
  const m_str line = STRING(code_obj);
  const m_uint ret = compile_string(shred->info->vm->gwion, "Machine.compile", line);
  if(ret > 0) *(m_int*)RETURN = ret;
  else xfun_handle(shred, "InvalidRuntimeCompilation");
}

static SFUN(machine_compile_values) {
  const M_Object code_obj = *(M_Object*)MEM(0);
  const m_str line = STRING(code_obj);
  const M_Vector names = ARRAY(*(M_Object*)MEM(SZ_INT));
  ValueList *const values = get_values(shred, names);
  const m_uint ret = compile_string_values(shred->info->vm->gwion, "Machine.compile", line, values);
  free_valuelist(shred->info->mp, values);
  if(ret > 0) *(m_int*)RETURN = ret;
  else xfun_handle(shred, "InvalidRuntimeCompilation");
}

static SFUN(machine_compile_replace) {
  const m_int xid = *(m_int*)MEM(0);
  if(!vm_remove(shred->info->vm, xid))
    xfun_handle(shred, "InvalidShredRemove");
  const M_Object code_obj = *(M_Object*)MEM(SZ_INT);
  const m_str line = STRING(code_obj);
  const m_uint ret = compile_string_xid(shred->info->vm->gwion, "Machine.compile", line, xid);
  if(ret > 0) *(m_int*)RETURN = ret;
  else xfun_handle(shred, "InvalidRuntimeCompilation");
}

static SFUN(machine_compile_replace_values) {
  const m_int xid = *(m_int*)MEM(0);
  if(!vm_remove(shred->info->vm, xid))
    xfun_handle(shred, "InvalidShredRemove");
  const M_Object code_obj = *(M_Object*)MEM(SZ_INT);
  const m_str line = STRING(code_obj);
  const M_Vector names = ARRAY(*(M_Object*)MEM(SZ_INT*2));
  ValueList *const values = get_values(shred, names);
  const m_uint ret = compile_string_xid_values(shred->info->vm->gwion, "Machine.compile", line, xid, values);
  free_valuelist(shred->info->mp, values);
  if(ret > 0) *(m_int*)RETURN = ret;
  else xfun_handle(shred, "InvalidRuntimeCompilation");
}

static SFUN(machine_shreds) {
  VM* vm = shred->info->vm;
  const Type t = array_type(shred->info->vm->gwion->env, shred->info->vm->gwion->type[et_int], 1, (loc_t){});
  const Vector v = &vm->shreduler->active_shreds;
  const M_Object obj = new_array(shred->info->mp, t, vector_size(v));
  for(m_uint i = 0; i < vector_size(v); i++) {
    const VM_Shred sh = (VM_Shred)vector_at(v, i);
    m_vector_set(ARRAY(obj), i, &sh->tick->xid);
  }
  *(M_Object*)RETURN = obj;
}

static SFUN(machine_pass) {
  const M_Object array_obj = *(M_Object*)MEM(0);
  M_Vector array = ARRAY(array_obj);
  struct Vector_ v;
  for(m_uint i = 0; i < m_vector_size(array); ++i) {
    m_str str;
    m_vector_get(array, i, &str);
    vector_add(&v, (vtype)str);
  }
  const bool ret = pass_set(shred->info->vm->gwion, &v);
  vector_release(&v);
  if(!ret) xfun_handle(shred, "InvalidPassSet");
}

GWION_IMPORT(Machine) {
  gwidoc(gwi, "A namespace for machine operations");

  GWI_B(gwi_struct_ini(gwi, "Machine"))

  gwidoc(gwi, "compile and add a file");
  gwi_func_ini(gwi, "int",  "add");
  gwi_func_arg(gwi, "string",  "filename");
  GWI_B(gwi_func_end(gwi, machine_add, ae_flag_static))

  gwidoc(gwi, "compile and add a file with values");
  gwi_func_ini(gwi, "int",  "add_values:[...]");
  gwi_func_arg(gwi, "string",  "filename");
  gwi_func_arg(gwi, "string[]",  "varnames");
  GWI_B(gwi_func_end(gwi, machine_add_values, ae_flag_static))

  gwidoc(gwi, "remove a shred by id");
  gwi_func_ini(gwi, "void",  "remove");
  gwi_func_arg(gwi, "int", "id");
  GWI_B(gwi_func_end(gwi, machine_remove, ae_flag_static))

  gwidoc(gwi, "replace a shred with a new one from file");
  gwi_func_ini(gwi, "void",  "replace");
  gwi_func_arg(gwi, "int", "id");
  gwi_func_arg(gwi, "string", "filename");
  GWI_B(gwi_func_end(gwi, machine_replace, ae_flag_static))

  gwidoc(gwi, "replace a shred with a new one from file with values");
  gwi_func_ini(gwi, "void",  "replace_values");
  gwi_func_arg(gwi, "int", "id");
  gwi_func_arg(gwi, "string", "filename");
  gwi_func_arg(gwi, "string[]",  "varnames");
  GWI_B(gwi_func_end(gwi, machine_replace_values, ae_flag_static))

  gwidoc(gwi, "get an array of active shred ids");
  gwi_func_ini(gwi, "int[]", "shreds");
  GWI_B(gwi_func_end(gwi, machine_shreds, ae_flag_static))

  gwidoc(gwi, "compile code");
  gwi_func_ini(gwi, "int",  "check");
  gwi_func_arg(gwi, "string", "code");
  GWI_B(gwi_func_end(gwi, machine_check, ae_flag_static))

  gwidoc(gwi, "compile code with values");
  gwi_func_ini(gwi, "int",  "check");
  gwi_func_arg(gwi, "string", "code");
  gwi_func_arg(gwi, "string[]",  "varnames");
  GWI_B(gwi_func_end(gwi, machine_check_values, ae_flag_static))

  gwidoc(gwi, "compile file");
  gwi_func_ini(gwi, "int", "compile");
  gwi_func_arg(gwi, "string", "code");
  GWI_B(gwi_func_end(gwi, machine_compile, ae_flag_static))

  gwidoc(gwi, "compile file with values");
  gwi_func_ini(gwi, "int", "compile");
  gwi_func_arg(gwi, "string", "code");
  gwi_func_arg(gwi, "string[]",  "varnames");
  GWI_B(gwi_func_end(gwi, machine_compile_values, ae_flag_static))

  gwidoc(gwi, "remove a shred and replace it by compiling file");
  gwi_func_ini(gwi, "void", "compile_replace");
  gwi_func_arg(gwi, "int", "xid");
  gwi_func_arg(gwi, "string", "filename");
  GWI_B(gwi_func_end(gwi, machine_compile_replace, ae_flag_static))

  gwidoc(gwi, "remove a shred and replace it by compiling file with values");
  gwi_func_ini(gwi, "void", "compile_replace");
  gwi_func_arg(gwi, "int", "xid");
  gwi_func_arg(gwi, "string", "filename");
  gwi_func_arg(gwi, "string[]",  "varnames");
  GWI_B(gwi_func_end(gwi, machine_compile_replace_values, ae_flag_static))

  gwidoc(gwi, "get list of passes as a string");
  gwi_func_ini(gwi, "void", "pass");
  gwi_func_arg(gwi, "string[]", "passes");
  GWI_B(gwi_func_end(gwi, machine_pass, ae_flag_static))

  GWI_B(gwi_class_end(gwi))
  return true;
}
