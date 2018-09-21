#include <stdlib.h>
#include <string.h>
#include "type.h"
#include "instr.h"
#include "import.h"
#include "hash.h"
#include "scanner.h"
#include "compile.h"
#include "traverse.h"

#ifdef JIT
#include "jitter.h"
#endif
static SFUN(machine_add) {
  const M_Object obj = *(M_Object*)MEM(SZ_INT);
  if(!obj)
    return;
  const m_str str = STRING(obj);
  _release(obj, shred);
  if(!str)
    return;
  *(m_uint*)RETURN = compile(shred->vm_ref, str);
}

static SFUN(machine_check) {
  *(m_uint*)RETURN = -1;
  const M_Object code_obj = *(M_Object*)MEM(SZ_INT);
  const m_str line = code_obj ? STRING(code_obj) : NULL;
  release(code_obj, shred);
  if(!line)return;
  FILE* f = fmemopen(line, strlen(line), "r");
  const Ast ast = parse(shred->vm_ref->scan, "Machine.check", f);
  if(!ast)
    goto close;
  *(m_uint*)RETURN = traverse_ast(shred->vm_ref->emit->env, ast);
close:
  if(ast)
    free_ast(ast);
  fclose(f);
}

static SFUN(machine_compile) {
  *(m_uint*)RETURN = -1;
  const M_Object code_obj = *(M_Object*)MEM(SZ_INT);
  const m_str line = code_obj ? STRING(code_obj) : NULL;
  release(code_obj, shred);
  if(!line)return;
  FILE* f = fmemopen(line, strlen(line), "r");
  const Ast ast = parse(shred->vm_ref->scan, "Machine.compile", f);
  if(!ast)
    goto close;
  const m_str str = strdup("Machine.compile");
  if(traverse_ast(shred->vm_ref->emit->env, ast) < 0) {
    free(str);
    goto close;
  }
  *(m_uint*)RETURN = emit_ast(shred->vm_ref->emit, ast, str);
#ifdef JIT
  /*shred->vm_ref->emit->jit->wait = 1;*/
  /*pthread_barrier_wait(&shred->vm_ref->emit->jit->barrier);*/
#endif
  emitter_add_instr(shred->vm_ref->emit, EOC);
  shred->vm_ref->emit->code->name = strdup(str);
  const VM_Code code = emit_code(shred->vm_ref->emit);
  const VM_Shred sh = new_vm_shred(code);
  vm_add_shred(shred->vm_ref, sh);
  free(str);
close:
  if(ast)
    free_ast(ast);
  fclose(f);
}

static SFUN(machine_shreds) {
  VM* vm = shred->vm_ref;
  const Type t = array_type(t_int, 1);
  const M_Object obj = new_array(t, SZ_INT, vec_size(&vm->shred), 1);
  for(m_uint i = 0; i < vec_size(&vm->shred); i++) {
    const VM_Shred sh = (VM_Shred)vec_at(&vm->shred, i);
    vm_vec_set(ARRAY(obj), i, &sh->xid);
  }
  vec_add(&shred->gc, (vtype)obj);
  *(M_Object*)RETURN = obj;
}

GWION_IMPORT(machine) {
  Type t_machine;
  CHECK_OB((t_machine = gwi_mk_type(gwi, "Machine", 0, NULL)))
  CHECK_BB(gwi_class_ini(gwi,  t_machine, NULL, NULL))
  gwi_func_ini(gwi, "void",  "add", machine_add);
  gwi_func_arg(gwi,       "string",  "filename");
  CHECK_BB(gwi_func_end(gwi, ae_flag_static))

  gwi_func_ini(gwi, "int[]", "shreds", machine_shreds);
  CHECK_BB(gwi_func_end(gwi, ae_flag_static))

  gwi_func_ini(gwi, "int",  "check", machine_check);
  gwi_func_arg(gwi,      "string",  "code");
  CHECK_BB(gwi_func_end(gwi, ae_flag_static))

  gwi_func_ini(gwi, "void", "compile", machine_compile);
  gwi_func_arg(gwi,      "string",  "filename");
  CHECK_BB(gwi_func_end(gwi, ae_flag_static))

  CHECK_BB(gwi_class_end(gwi))
  return 1;
}
