#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "instr.h"
#include "emit.h"
#include "vm.h"
#include "gwion.h"
#include "object.h"
#include "operator.h"
#include "import.h"
#include "gwi.h"
#include "traverse.h"
#include "object.h"
#include "parse.h"
#include "array.h"
#include "operator.h"

#define TUPLE_NAME "Tuple"

#undef insert_symbol

struct TupleEmit {
  Exp* e;
  Vector v;
  m_uint obj_offset;
  m_uint tmp_offset;
  m_uint mem_offset;
  m_uint sz;
  m_uint idx;
};

struct UnpackInfo_ {
  m_uint obj_offset;
  m_uint mem_offset;
  m_uint size;
};

static INSTR(TupleUnpack) {
  const M_Object o = *(M_Object*)(shred->reg - SZ_INT);
  struct UnpackInfo_ *info = (struct UnpackInfo_*)instr->m_val;
  memcpy(shred->mem + info->mem_offset, o->data + info->obj_offset, info->size);
}

ANN static inline Type tuple_base(Type t, const m_uint idx) {
  while(t->info->tuple->start > idx)
    t = t->info->parent;
  return t;
}

static INSTR(TupleMember) {
  const M_Object o = *(M_Object*)(shred->reg - SZ_INT);
  const Type base = tuple_base(o->type_ref, instr->m_val);
  const m_bit* byte = shred->code->bytecode + (shred->pc-1) * BYTECODE_SZ;
  const m_uint idx = instr->m_val - base->info->tuple->start;
  const Type t = (Type)vector_at(&base->info->tuple->types, idx);
  const m_uint offset = vector_at(&base->info->tuple->offset, idx);
  *(m_uint*)(byte + SZ_INT) = offset;
  if(!instr->m_val2) {
    if(t->size == SZ_INT)
      *(m_uint*)(byte) = eDotMember;
    else if(t->size == SZ_FLOAT)
      *(m_uint*)(byte) = eDotMember2;
    else {
      *(m_uint*)(byte) = eDotMember3;
      *(m_uint*)(byte + SZ_INT*2) = t->size;
    }
  } else
    *(m_uint*)(byte) = eDotMember4;
}

static FREEARG(freearg_tuple_at) {
  mp_free(((Gwion)gwion)->mp, UnpackInfo, (struct UnpackInfo*)instr->m_val);
}

ANN static void emit_unpack_instr_inner(const Emitter emit, struct TupleEmit *te) {
  const Instr instr = emit_add_instr(emit, TupleUnpack);
  struct UnpackInfo_ *info = mp_malloc(emit->gwion->mp, UnpackInfo);
  info->obj_offset = te->obj_offset;
  info->mem_offset = te->mem_offset;
  info->size = te->sz;
  instr->m_val = (m_uint)info;
}

ANN static int tuple_continue(struct TupleEmit *te) {
  const bool ret = (te->e = te->e->next) &&
       ++te->idx < VLEN(te->v);
  if(!ret)
    te->e = NULL;
  return ret;
}

ANN static void unpack_instr_decl(const Emitter emit, struct TupleEmit *te) {
  m_uint sz = 0;
  te->sz = 0;
  te->obj_offset = te->tmp_offset;
  do {
    if(te->e->exp_type == ae_exp_decl) {
      const Value value = te->e->d.exp_decl.var.vd.value;
      te->sz += value->type->size;
      sz += value->type->size;
      value->from->offset = emit_local(emit, value->type);
    } else {
      sz += ((Type)vector_at(te->v, te->idx))->size;
      break;
    }
  } while(tuple_continue(te));
  te->tmp_offset += sz;
}

ANN void emit_unpack_instr(const Emitter emit, struct TupleEmit *te) {
  te->mem_offset = emit_code_offset(emit);
  unpack_instr_decl(emit, te);
  if(te->sz)
    emit_unpack_instr_inner(emit, te);
  if(te->e && (te->e = te->e->next))
    emit_unpack_instr(emit, te);
}

static void get(const Vector v, Type t) {
  if(t->info->parent)
    get(v, t->info->parent);
  if(!t->info->tuple) // necessary ?
    return;
  for(m_uint i = 0; i < vector_size(&t->info->tuple->types); ++i)
    vector_add(v, (m_uint)vector_at(&t->info->tuple->types, i));
}

struct Matcher {
  struct Vector_ l;
  struct Vector_ r;
  const Type undef;
};

ANN static inline bool matcher_run(struct Matcher *m) {
  for(m_uint i = 0; i < vector_size(&m->r); ++i) {
    DECL_B(const Type, l, = (Type)vector_at(&m->l, i));
    DECL_B(const Type, r, = (Type)vector_at(&m->r, i));
    if(r != m->undef)
      CHECK_B(isa(l, r));
  }
  return true;
}

static inline void matcher_init(struct Matcher *m) {
  vector_init(&m->l);
  vector_init(&m->r);
}

static inline void matcher_release(struct Matcher *m) {
  vector_release(&m->l);
  vector_release(&m->r);
}

static bool tuple_match(const Env env, const Type lhs, const Type rhs, const loc_t loc) {
  struct Matcher m = { .undef= env->gwion->type[et_auto] };
  matcher_init(&m);
  get(&m.l, lhs);
  get(&m.r, rhs);
  const bool ret = matcher_run(&m);
  matcher_release(&m);
  if(ret) return true;
  ERR_B(loc, _("Tuple types '%s' and '%s' do not match"), lhs->name, rhs->name);
}

static OP_CHECK(opck_at_object_tuple) {
  const Exp_Binary *bin = (Exp_Binary*)data;
  CHECK_NN(opck_rassign(env, data));
  CHECK_ON(tuple_match(env, bin->lhs->type, bin->rhs->type, exp_self(bin)->loc));
  exp_setvar(bin->rhs, 1);
  return bin->rhs->type;
}

/*
static OP_EMIT(opem_at_object_tuple) {
  const Exp_Binary *bin = (Exp_Binary*)data;
  const Instr instr = emit_add_instr(emit, TupleAt);
  instr->m_val = (m_uint)bin->rhs->type;
  return true;
}

static OP_CHECK(opck_at_tuple_object) {
  const Exp_Binary *bin = (Exp_Binary*)data;
  CHECK_NN(opck_rassign(env, data));
  if(!bin->rhs->type->info->tuple)
    return bin->rhs->type;
  CHECK_0N(tuple_match(env, bin->rhs->type, bin->lhs->type, exp_self(bin)->loc));
  exp_setvar(bin->rhs, 1);
  set_decl_ref(bin->rhs);
  return bin->rhs->type;
}

static OP_CHECK(opck_cast_tuple_object) {
  const Exp_Cast *cast = (Exp_Cast*)data;
  CHECK_ON(tuple_match(env, exp_self(cast)->type, cast->exp->type, exp_self(cast)->loc));
  return exp_self(cast)->type;
}

static INSTR(Tuple2Object) {
  const M_Object o = *(M_Object*)(shred->reg - instr->m_val2);
  const Type t = (Type)instr->m_val;
  if(o && !isa(o->type_ref, t))
  // TODO: pass position by m_val2
    handle(shred, _("TupleCast\n"));
}
*/
/*
// TODO: do not emit Tuple2Object if full match
static OP_EMIT(opem_cast_tuple_object) {
  const Exp_Cast* exp = (Exp_Cast*)data;
  const Instr instr = emit_add_instr(emit, Tuple2Object);
  instr->m_val = (m_uint)exp_self(exp)->type;
  instr->m_val2 = SZ_INT;
  return true;
}
*/

static OP_CHECK(opck_cast_tuple) {
  const Exp_Cast *cast = (Exp_Cast*)data;
  CHECK_ON(tuple_match(env, cast->exp->type, exp_self(cast)->type, exp_self(cast)->loc));
  return exp_self(cast)->type;
}

static OP_CHECK(opck_impl_tuple) {
  struct Implicit *imp = (struct Implicit*)data;
  CHECK_ON(tuple_match(env, imp->e->type, imp->t, imp->e->loc));
  return imp->t;
}

static INSTR(TupleCtor) {
// most of this could be expressed with regular instructions
  const Type t = (Type)instr->m_val;
  const M_Object o = new_object(shred->info->mp, t);
  const m_uint sz = t->nspc->offset;
  memcpy(o->data, shred->reg - sz - SZ_INT, sz);
  shred->reg -= (sz);
  *(M_Object*)(shred->reg - SZ_INT) = o;
}

ANN static Symbol tuple_sym(const Env env, const Vector v) {
  GwText text;
  text_init(&text, env->gwion->mp);
  text_add(&text, TUPLE_NAME);
  text_add(&text, ":[");
  for(m_uint i = 0; i < vector_size(v); ++i) {
    const Type t = (Type)vector_at(v, i);
    if(t != (Type)1) {
      loc_t loc = {};
      const m_str str = type2str(env->gwion, t, loc);
      text_add(&text, str);
      free_mstr(env->gwion->mp, str);
    } else
      text_add(&text, "_");
    if(i+1 < vector_size(v))
      text_add(&text, ",");
  }
  text_add(&text, "]");
  const Symbol sym = insert_symbol(env->gwion->st, text.str);
  text_release(&text);
  return sym;
}

ANN static Exp* decl_from_id(const Gwion gwion, const Type type, Symbol name, const loc_t loc) {
  Type_Decl *td = type != (Type)1 ?
      type2td(gwion, type, loc) :
      new_type_decl(gwion->mp, insert_symbol(gwion->st, "@Undefined"), loc);
  td->flag -= ae_flag_late;
  struct Var_Decl_ vd = { .tag = MK_TAG(name, loc) };
  return new_exp_decl(gwion->mp, td, &vd, loc);
}

ANN Type tuple_type(const Env env, const Vector v, const loc_t loc) {
  const Symbol sym = tuple_sym(env, v);
  const Type exists = nspc_lookup_type1(env->curr, sym);
  if(exists)
    return exists;
  StmtList *base = NULL;
  for(m_uint i = 0; i < vector_size(v); ++i) {
    char name[num_digit(i) + 16];
    sprintf(name, "@tuple member %"UINT_F, i);
    const Symbol sym = insert_symbol(env->gwion->st, name);
    const Type t = (Type)vector_at(v, i);
    Exp* decl = decl_from_id(env->gwion, t, sym, loc);
    Stmt stmt = MK_STMT_EXP(loc, decl);
    if(base) {
      stmtlist_add(env->gwion->mp, &base, stmt);
    } else {
      base = new_stmtlist(env->gwion->mp, 1);
      stmtlist_set(base, 0, stmt);
    }
  }
  Section section = MK_SECTION(stmt, stmt_list, base, loc);
  Ast body = new_sectionlist(env->gwion->mp, 1);
  sectionlist_set(body, 0, section);
  Type_Decl *td = new_type_decl(env->gwion->mp, insert_symbol(env->gwion->st, TUPLE_NAME), loc);
  Class_Def cdef = new_class_def(env->gwion->mp, ae_flag_none, MK_TAG(sym, loc), td, body);
  SET_FLAG(cdef, abstract | ae_flag_final);
  CHECK_O(scan0_class_def(env, cdef));
//  SET_FLAG(cdef->base.type, abstract | ae_flag_final | ae_flag_late);
//  set_tflag(cdef->base.type, tflag_empty);
//const m_uint scope = env_push_nspc(env, env->global_nspc);
  CHECK_O(traverse_class_def(env, cdef));
//env_pop(env, scope);
  return cdef->base.type;
}

ANN Type check_array_access(const Env env, const Array_Sub array);

static OP_CHECK(opck_tuple) {
  const Array_Sub array = (Array_Sub)data;
  Exp* exp = array->exp;
  if(exp->exp_type != ae_exp_primary ||
     exp->d.prim.prim_type != ae_prim_num)
    ERR_O(exp->loc, _("tuple subscripts must be litteral"));
  const m_uint idx = exp->d.prim.d.gwint.num;
  const Vector v = array->type->info->tuple ?
    &array->type->info->tuple->types : NULL;
  if(!v || idx >= vector_size(v))
    ERR_O(exp->loc, _("tuple subscripts too big"));
  const Type type = (Type)vector_at(v, idx);
  if(type == env->gwion->type[et_auto])
    ERR_O(exp->loc, _("tuple subscripts is undefined at index %"UINT_F), idx);
  if(!exp->next)
    return type;
  struct Array_Sub_ next = { exp->next, type, array->depth - 1 };
  return check_array_access(env, &next);
}

static OP_CHECK(opck_tuple_ctor) {
  const Exp_Call *call = (Exp_Call*)data;
  Exp* exp = call->args;
  if(exp)
    CHECK_O(check_exp(env, exp));
  struct Vector_ v;
  vector_init(&v);
  Exp* e = exp;
  do vector_add(&v, (m_uint)e->type);
  while((e = e->next));
  const Type ret = tuple_type(env, &v, exp_self(call)->loc);
  vector_release(&v);
  return ret;
}

static OP_EMIT(opem_tuple_ctor) {
  const Exp_Call *call = (Exp_Call*)data;
  Exp* exp = call->args;
  m_int sz = -exp_self(call)->type->nspc->offset;
  while(exp) {
//    const Type t = exp->cast_to ?: exp->type;
//    if(tflag(t, tflag_release))
//      (void)emit_compound_addref(emit, t, sz, false);
    sz += exp->type->size;
    exp = exp->next;
  }
  const Instr instr = emit_add_instr(emit, TupleCtor);
  instr->m_val = (m_uint)exp_self(call)->type;
  return true;
}

static OP_CHECK(unpack_ck) {
  const Exp_Call *call = (Exp_Call*)data;
  const Symbol decl = insert_symbol(env->gwion->st, "auto");
  const Symbol skip = insert_symbol(env->gwion->st, "_");
  Exp* e = call->args;
  while(e) {
    if(e->exp_type != ae_exp_primary || e->d.prim.prim_type != ae_prim_id)
      ERR_O(e->loc, _("invalid expression for unpack"));
    if(e->d.prim.d.var != skip) {
      const Symbol var = e->d.prim.d.var;
      memset(&e->d, 0, sizeof(union exp_data));
      e->type = env->gwion->type[et_auto];
      e->d.exp_decl.type = env->gwion->type[et_auto];
      e->exp_type = ae_exp_decl;
      e->d.exp_decl.var.td = new_type_decl(env->gwion->mp, decl, e->loc);
      e->d.exp_decl.var.vd = (Var_Decl){ .tag = MK_TAG(var, e->loc)};
    } else {
      e->d.prim.prim_type = ae_prim_nil;
//      e->type = env->gwion->type[et_auto];
      e->type = env->gwion->type[et_void];
    }
    e = e->next;
  }
  exp_setmeta(exp_self(call), 0);
  return _class_base(call->func->type);
}

static OP_EMIT(unpack_em) {
  const Exp_Call *call = (Exp_Call*)data;
  if(exp_getmeta(exp_self(call)))
    return true;
  env_err(emit->env, exp_self(call)->loc, _("unused Tuple unpack"));
  return false;
}

static void parents(const Env env, const Type t, const Vector v) {
  const Nspc parent = t->info->value->from->owner;
  if(parent->parent && parent != env->context->nspc && parent != env->global_nspc) {
    const Type older = nspc_lookup_type1(parent->parent, insert_symbol(env->gwion->st, parent->name));
    parents(env, older, v);
  }
  vector_add(v, (vtype)insert_symbol(env->gwion->st, t->name));
}

static OP_CHECK(opck_at_unpack) {
  const Exp_Binary *bin = (Exp_Binary*)data;
  Exp* e = bin->rhs->d.exp_call.args;
  int i = 0;
  while(e) {
    if(e->exp_type == ae_exp_decl) {
      DECL_O(const Type, t, = (Type)VPTR(&bin->lhs->type->info->tuple->types, i));
      struct Vector_ v; // hoist?
      vector_init(&v);
      parents(env, t, &v);
      Type_Decl *td = e->d.exp_decl.var.td;
      td->tag.sym = (Symbol)vector_front(&v);
      for(m_uint i = 1; i < vector_size(&v); ++i)
        td = (td->next = new_type_decl(env->gwion->mp, (Symbol)vector_at(&v, i), e->loc));
      vector_release(&v);
      Exp* next = e->next;
      e->d.exp_decl.type = NULL;
      e->next = NULL;
      e->type = NULL;
      const bool ret = traverse_exp(env, e);
      e->next = next;
      CHECK_O(ret);
      exp_setmeta(bin->rhs, 1);
    }
    ++i;
    e = e->next;
  }
  return bin->lhs->type;
}

static OP_EMIT(opem_at_unpack) {
  const Exp_Binary *bin = (Exp_Binary*)data;
  if(bin->rhs->d.exp_call.args) {
    const Type t_undef = emit->gwion->type[et_auto];
    Exp* e = bin->rhs->d.exp_call.args;
    m_uint sz = 0;
    do if(e->type != t_undef)
      sz += e->type->size;
    while((e = e->next));
    emit_regmove(emit, -sz);
    const Vector v = &bin->lhs->type->info->tuple->types;
    struct TupleEmit te = { .e=bin->rhs->d.exp_call.args, .v=v };
    emit_unpack_instr(emit, &te);
//    const Instr pop2 = emit_add_instr(emit, RegMove);
//    pop2->m_val = -SZ_INT;

  }
  return true;
}

static ANN Type scan_tuple(const Env env, const Type_Decl *td) {
  struct Vector_ v;
  vector_init(&v);
  TmplArgList *tl = td->types;
  for(uint32_t i = 0; i < tl->len; i++) {
    TmplArg arg = tmplarglist_at(tl, i);
    // TODO: beware arg type!!!
    Type_Decl *td = arg.d.td;
    const Type t = td->tag.sym != insert_symbol(env->gwion->st, "_") ?
       known_type(env, td) : (Type)1;
    if(t)
      vector_add(&v, (m_uint)t);
    else {
      vector_release(&v);
      return env->gwion->type[et_error];
    }
  }
  const Type ret = tuple_type(env, &v, td->tag.loc);
  vector_release(&v);
  return ret;
}

static OP_CHECK(opck_tuple_scan) {
  struct TemplateScan *ts = (struct TemplateScan*)data;
  return ts->td->types ? scan_tuple(env, ts->td) : ts->t;
}

ANN static void tuple_access(const  Emitter emit, const m_uint idx,
        const bool is_var) {
  const Instr instr = emit_add_instr(emit, TupleMember);
  instr->m_val = idx;
  instr->m_val2 = is_var;
  emit_add_instr(emit, DotMember); // just a place holder.
}

static OP_EMIT(opem_tuple_access) {
  struct ArrayAccessInfo *info = (struct ArrayAccessInfo*)data;
  const m_uint idx = info->array.exp->d.prim.d.gwint.num;
  tuple_access(emit, idx, (info->array.depth -1)? 0 : info->is_var);
  if(!info->array.exp->next)
    return true;
  const Type type = (Type)vector_at(&info->array.type->info->tuple->types, idx);
  struct Array_Sub_ next = { info->array.exp->next, type, info->array.depth - 1 };
  info->array = next;
  return emit_array_access(emit, info);
}

GWION_IMPORT(Tuple) {
  const Type t_tuple = gwi_class_ini(gwi, TUPLE_NAME, "Object");
  GWI_B(gwi_func_ini(gwi, "auto", "new"))
//   GWI_B(gwi_oper_add(gwi, opck_tuple_ctor))
//   GWI_B(gwi_oper_emi(gwi, opem_tuple_ctor))
  GWI_B(gwi_func_end(gwi, (f_xfun)1, ae_flag_none))
  GWI_B(gwi_class_end(gwi));

  const Type t_undef = gwi_mk_type(gwi, "@Undefined", SZ_INT, NULL);
  gwi_add_type(gwi, t_undef);
  SET_FLAG(t_tuple, abstract);
  set_tflag(t_tuple, tflag_tmpl);
  set_tflag(t_tuple, tflag_ntmpl);

   GWI_B(gwi_oper_ini(gwi, NULL, TUPLE_NAME, NULL));
   GWI_B(gwi_oper_add(gwi, opck_tuple_ctor))
   GWI_B(gwi_oper_emi(gwi, opem_tuple_ctor))
   GWI_B(gwi_oper_end(gwi, "call_type", NULL))
   GWI_B(gwi_oper_ini(gwi, TUPLE_NAME, NULL, NULL))
   GWI_B(gwi_oper_add(gwi, opck_tuple_scan))
   GWI_B(gwi_oper_end(gwi, "class", NULL))

   GWI_B(gwi_oper_ini(gwi, "Object", TUPLE_NAME, NULL))
   GWI_B(gwi_oper_add(gwi, opck_at_object_tuple))
   GWI_B(gwi_oper_end(gwi, "=>", NULL))
   GWI_B(gwi_oper_add(gwi, opck_cast_tuple))
  gwi_oper_eff(gwi, "TupleCast");
   GWI_B(gwi_oper_end(gwi, "$", NoOp))
   GWI_B(gwi_oper_add(gwi, opck_impl_tuple))
   GWI_B(gwi_oper_end(gwi, "@implicit", NULL))
//   GWI_B(gwi_oper_ini(gwi, TUPLE_NAME, "Object", NULL))
//   GWI_B(gwi_oper_add(gwi, opck_at_tuple_object))
//   GWI_B(gwi_oper_emi(gwi, opem_at_tuple_object))
//   GWI_B(gwi_oper_end(gwi, "=>", NULL))
//   GWI_B(gwi_oper_add(gwi, opck_cast_tuple_object))
//   GWI_B(gwi_oper_emi(gwi, opem_cast_tuple_object))
//   GWI_B(gwi_oper_end(gwi, "$", NULL))
//   GWI_B(gwi_oper_add(gwi, opck_impl_tuple))
//   GWI_B(gwi_oper_end(gwi, "@implicit", NULL))
//   GWI_B(gwi_oper_ini(gwi, TUPLE_NAME, "Tuple", NULL))
//   GWI_B(gwi_oper_add(gwi, opck_at_object_tuple))
//   GWI_B(gwi_oper_end(gwi, "=>", ObjectAssign))
   GWI_B(gwi_oper_ini(gwi, "int", TUPLE_NAME, NULL))
   GWI_B(gwi_oper_add(gwi, opck_tuple))
   GWI_B(gwi_oper_emi(gwi, opem_tuple_access))
   GWI_B(gwi_oper_end(gwi, "[]", NULL))
  gwi_register_freearg(gwi, TupleUnpack, freearg_tuple_at);

  const Type t_unpack = gwi_mk_type(gwi, "Unpack", 0, NULL);
  SET_FLAG(t_unpack, abstract | ae_flag_final);
  gwi_add_type(gwi, t_unpack);
   GWI_B(gwi_oper_ini(gwi, NULL, "Unpack", NULL))
   GWI_B(gwi_oper_add(gwi, unpack_ck))
   GWI_B(gwi_oper_emi(gwi, unpack_em))
   GWI_B(gwi_oper_end(gwi, "@ctor", NULL))
   GWI_B(gwi_oper_ini(gwi, "Unpack", NULL, NULL))
   GWI_B(gwi_oper_add(gwi, unpack_ck))
   GWI_B(gwi_oper_end(gwi, "@partial", NULL))
   GWI_B(gwi_oper_ini(gwi, "Object", "Unpack", NULL))
   GWI_B(gwi_oper_add(gwi, opck_at_unpack))
   GWI_B(gwi_oper_emi(gwi, opem_at_unpack))
   GWI_B(gwi_oper_end(gwi, "=>", NULL))

//   GWI_B(gwi_oper_add(gwi, opck_at_unpack))
//   GWI_B(gwi_oper_emi(gwi, opem_at_unpack))
//   GWI_B(gwi_oper_end(gwi, "==", NULL))
  return true;
}
