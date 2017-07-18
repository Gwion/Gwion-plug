#include "map.h"
#include "vm.h"
#include "type.h"
#include "dl.h"
#include "err_msg.h"
#include "import.h"
#include "lang.h"
#include "vm.h"
#include "shreduler.h"
#include "ugen.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "lsys.h"

static struct Type_ t_lsys = {"Lsys", SZ_INT, &t_ugen };

typedef struct
{
	lsys_d       lsys;
	lsys_list    lst;
	lsys_entry * ent;
	m_uint pos;
	m_bool is_init;
} Lsys;

#define LSYS(o) ((Lsys*)UGEN(o)->ug)

static int tochar(int c) {
    if(c >= 10) {
        return c + 87;
    } else {
        return c + 48;
    }
}

TICK(tick)
{
	Lsys* ptr = (Lsys*)u->ug;
	u->out = 0;
	if(!ptr->is_init || !u->trig)
		return 1;
	base_tick(UGEN(u->trig));
	if(UGEN(u->trig)->out)
	{
		ptr->pos = lsys_list_iter(&ptr->lst, &ptr->ent, ptr->pos);
		u->out = ptr->ent->val + 1;
	}
	return 1;
}

static CTOR(ctor)
{
	Lsys* l = (Lsys*) malloc(sizeof(Lsys));
	lsys_init(&l->lsys);
	l->is_init = 0;
	l->pos = 0;
	assign_ugen(UGEN(o), 0, 1, 1, l);
	UGEN(o)->tick = tick;
}

static DTOR(dtor)
{
	Lsys* l = LSYS(o);
	if(l->is_init)
		lsys_list_destroy(&l->lst);
	free(l);
}

static MFUN(gw_lsys_parse)
{
	Lsys*  ptr = LSYS(o);
	m_uint ord = *(m_uint*)MEM(SZ_INT);
	m_str  str = STRING(*(M_Object*)MEM(SZ_INT*2));

	lsys_parse(&ptr->lsys, str, str, strlen(str));
	lsys_list_init(&ptr->lst);
	lsys_make_list(&ptr->lsys, &ptr->lst, str, 0, ord);
	lsys_list_reset(&ptr->lst);

	ptr->is_init = 1;
	ptr->pos     = 0;
}

static MFUN(gw_lsys_reset)
{
	Lsys*  ptr = LSYS(o);
	if(ptr->is_init);
		lsys_list_reset(&ptr->lst);
}

static MFUN(gw_lsys_size)
{
	Lsys*  ptr = LSYS(o);
	RETURN->d.v_uint = ptr->is_init ? ptr->lst.size : -1;
}

static MFUN(gw_lsys_get)
{
	int i;
	Lsys*  ptr   = LSYS(o);
	if(!ptr->is_init)
	{
		RETURN->d.v_uint = 0;
		return;
	}
	char str[ptr->lst.size];
	for(i = 0; i < ptr->lst.size; i++) {
		ptr->pos = lsys_list_iter(&ptr->lst, &ptr->ent, ptr->pos);
		str[i] = tochar(ptr->ent->val + 1);
	}
	RETURN->d.v_uint = (m_uint)new_String(shred, str);
}

IMPORT
{
  DL_Func  fun;
	CHECK_BB(import_class_begin(env, &t_lsys, env->global_nspc, ctor, dtor))
	dl_func_init(&fun, "void", "parse", (m_uint)gw_lsys_parse);
		dl_func_add_arg(&fun, "int",    "ord");
		dl_func_add_arg(&fun, "string", "str");
	CHECK_BB(import_fun(env, &fun, 0))
	dl_func_init(&fun, "void", "reset", (m_uint)gw_lsys_reset);
	CHECK_BB(import_fun(env, &fun, 0))
	dl_func_init(&fun, "int", "size", (m_uint)gw_lsys_size);
	CHECK_BB(import_fun(env, &fun, 0))
	dl_func_init(&fun, "string", "get", (m_uint)gw_lsys_get);
	CHECK_BB(import_fun(env, &fun, 0))
	CHECK_BB(import_class_end(env))
	return 1;
}
