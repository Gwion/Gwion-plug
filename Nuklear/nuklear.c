#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "instr.h"
#include "gwion.h"
#include "object.h"
#include "operator.h"
#include "import.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_XLIB_IMPLEMENTATION
#define NK_IMPLEMENTATION
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"
#include "nuklear_xlib.h"

#define DTIME           20
#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   600

#define UNUSED(a) (void)a
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a)/sizeof(a)[0])


//#include "style.c"

#include "err_msg.h"
#include "instr.h"
#include "import.h"
#include "vm.h"

static m_uint active = 0;
typedef struct XWindow XWindow;
struct XWindow {
    Display *dpy;
    Window root;
    Visual *vis;
    Colormap cmap;
    XWindowAttributes attr;
    XSetWindowAttributes swa;
    Window win;
    int screen;
    XFont *font;
    unsigned int width;
    unsigned int height;
};

typedef struct GWindow {
  struct XWindow*    win;
  struct nk_context* ctx;
  M_Object last_widget;
  Vector widget;
  long started, dt, running;
  pthread_t thread;
} GWindow;

static GWindow* last_window = NULL;

static void // TODO use gw_err instead
die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs("\n", stderr);
    exit(EXIT_FAILURE);
}

static long
timestamp(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) return 0;
    return (long)((long)tv.tv_sec * 1000 + (long)tv.tv_usec/1000);
}

static void
sleep_for(long t)
{
    struct timespec req;
    const time_t sec = (int)(t/1000);
    const long ms = t - (sec * 1000);
    req.tv_sec = sec;
    req.tv_nsec = ms * 1000000L;
    while(-1 == nanosleep(&req, &req));
}

/* ===============================================================
 *
 *                          EXAMPLE
 *
 * ===============================================================*/
/* This are some code examples to provide a small overview of what can be
 * done with this library. To try out an example uncomment the include
 * and the corresponding function. */
/*#include "../style.c"*/
/*#include "../calculator.c"*/
/*#include "../overview.c"*/
/*#include "../node_editor.c"*/

/* ===============================================================
 *
 *                          DEMO
 *
 * ===============================================================*/
static m_int o_nk_data;
static m_int o_nk_name;
static m_int o_nk_list;
static m_int o_nk_ival;
  static m_int o_nk_imin;
  static m_int o_nk_imax;
  static m_int o_nk_istp;
  static m_int o_nk_iinc;
static m_int o_nk_fval;
  static m_int o_nk_fmin;
  static m_int o_nk_fmax;
  static m_int o_nk_fstp;
  static m_int o_nk_finc;
/*static m_int o_nk_sval;*/


#define LIST(o) *(Vector*)(o->data + o_nk_list)
#define NAME(o) STRING(*(M_Object*)(o->data + o_nk_name))
typedef void (*f_nk)(M_Object o, struct nk_context* ctx);


#define R(o) *(o->data + SZ_INT)
#define G(o) *(o->data + SZ_INT*2)
#define B(o) *(o->data + SZ_INT*3)
#define A(o) *(o->data + SZ_INT*4)
#define COLOR(o) (struct nk_color){ R(o), G(o), B(o), A(o) }

ANN static void widget_add(const Vector v, const M_Object o) {
  vector_add(v, (m_uint)o);
  ++o->ref;
}

static void* _loop(void* data) {
  GWindow* gw = (GWindow*)data;
  XWindow* xw = gw->win;
  struct nk_context* ctx = gw->ctx;
  xw->font = nk_xfont_create(xw->dpy, "fixed");
  ctx = nk_xlib_init(xw->font, xw->dpy, xw->screen, xw->win, xw->width, xw->height);
  gw->started = timestamp();
  while(1) {
    XLockDisplay(xw->dpy);
    XEvent evt;
    nk_input_begin(ctx);
    while (XCheckWindowEvent(xw->dpy, xw->win, xw->swa.event_mask, &evt)){
        if (XFilterEvent(&evt, xw->win)) continue;
        nk_xlib_handle_event(xw->dpy, xw->screen, xw->win, &evt);
    }
    nk_input_end(ctx);

        /* GUI */
/*        if (nk_window_is_closed(ctx, "Demo")) break;*/
          for(m_uint i = 0; i < vector_size(gw->widget); i++)
          {
            M_Object o = (M_Object)vector_at(gw->widget, i);
            f_nk exec = *(f_nk*)(o->data + SZ_INT*2);
            exec(o, ctx);
          }
        /* Draw */
        XClearWindow(xw->dpy, xw->win);
        nk_xlib_render(xw->win, nk_rgb(30,30,30));
        XFlush(xw->dpy);
        /* Timing */
        gw->dt = timestamp() - gw->started;
        if (gw->dt < DTIME)
            sleep_for(DTIME - gw->dt);
        XUnlockDisplay(xw->dpy);
    }
  return NULL;
}

static void gw_nk_init(GWindow* gw) {
  XWindow* xw = gw->win;
  if(!active)
    XInitThreads();
  gw->ctx = (struct nk_context*)xmalloc(sizeof(struct nk_context));
  if(!active) {
    xw->dpy = XOpenDisplay(NULL);
    xw->root = DefaultRootWindow(xw->dpy);
    xw->screen = XDefaultScreen(xw->dpy);
    xw->vis = XDefaultVisual(xw->dpy, xw->screen);
    xw->cmap = XCreateColormap(xw->dpy,xw->root,xw->vis,AllocNone);
    xw->swa.colormap = xw->cmap;
    xw->swa.event_mask =
    ExposureMask | KeyPressMask | KeyReleaseMask |
    ButtonPress | ButtonReleaseMask| ButtonMotionMask |
    Button1MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask|
    PointerMotionMask | KeymapStateMask;
    xw->win = XCreateWindow(xw->dpy, xw->root, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
      XDefaultDepth(xw->dpy, xw->screen), InputOutput,
      xw->vis, CWEventMask | CWColormap, &xw->swa);
  }
  else {
    xw->dpy    = last_window->win->dpy;
    xw->root   = last_window->win->root;
    xw->screen = last_window->win->screen;
    xw->vis = XDefaultVisual(xw->dpy, xw->screen);
    xw->cmap     = last_window->win->cmap;
    xw->cmap = XCreateColormap(xw->dpy,xw->root,xw->vis,AllocNone);
    xw->swa.colormap = xw->cmap;
    xw->swa.event_mask =
      ExposureMask | KeyPressMask | KeyReleaseMask |
      ButtonPress | ButtonReleaseMask| ButtonMotionMask |
      Button1MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask|
      PointerMotionMask | KeymapStateMask;
    xw->win = XCreateWindow(xw->dpy, xw->root, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
      XDefaultDepth(xw->dpy, xw->screen), InputOutput,
      xw->vis, CWEventMask | CWColormap, &xw->swa);
  }
  if(!xw->dpy) die("Could not open a display; perhaps $DISPLAY is not set?");
  char name[256];
  sprintf(name, "GWion: %p", gw);
  XStoreName(xw->dpy, xw->win, name);
  XMapWindow(xw->dpy, xw->win);
  XGetWindowAttributes(xw->dpy, xw->win, &xw->attr);
  xw->width = (unsigned int)xw->attr.width;
  xw->height = (unsigned int)xw->attr.height;
  ++active;
}

static CTOR(nk_ctor) {
  GWindow* gw = (GWindow*)xmalloc(sizeof(GWindow));
  gw->win = (XWindow*)xmalloc(sizeof(XWindow));
  gw->last_widget = NULL;
  gw->widget = new_vector(shred->info->mp);
  gw_nk_init(gw);
  last_window = gw;
  pthread_create(&gw->thread, NULL, &_loop, gw);
  *(GWindow**)(o->data + o_nk_data) = gw;
}

static void gw_nk_shutdown(struct XWindow* xw) {
//    nk_xlib_shutdown();   
//    XUnmapWindow(xw->dpy, xw->win);
//    XFreeColormap(xw.dpy, xw.cmap);
//    XDestroyWindow(xw.dpy, xw.win);
//    XCloseDisplay(xw.dpy); 
//  XUnmapWindow(xw->dpy, xw->win);
//  XDestroyWindow(xw->dpy, xw->win);
//  XFreeColormap(xw->dpy, xw->cmap);
//  nk_xfont_del(xw->dpy, xw->font);
//  XFlush(xw->dpy);
//  if(!--active) {
//    XCloseDisplay(xw->dpy);
//    nk_xfont_del(xw->dpy, xw->font);
//    nk_xlib_shutdown();
//    XUnmapWindow(xw->dpy, xw->win);
//    XFreeColormap(xw->dpy, xw->cmap);
//    XDestroyWindow(xw->dpy, xw->win);
//    XCloseDisplay(xw->dpy);
//  }
}
static DTOR(nk_dtor) {
  GWindow* gw = *(GWindow**)(o->data + o_nk_data);
  pthread_cancel(gw->thread);
  pthread_join(gw->thread, NULL);
  free(gw->ctx);
  for(m_uint i = vector_size(gw->widget) + 1; --i;)
    release((M_Object)vector_at(gw->widget, i-1), shred);
  vector_release(gw->widget);
//  XUnmapWindow(gw->win->dpy, gw->win->win);
//  XDestroyWindow(gw->win->dpy, gw->win->win);
  free(gw->win);
  free(gw);
  gw_nk_shutdown(gw->win);
}


static CTOR(widget_ctor)
{
  char name[256];
  sprintf(name, "Widget:%p", o);
  (*(M_Object*)(o->data + o_nk_name)) = new_string(shred->info->vm->gwion, name);
  (*(GWindow**)(o->data + SZ_INT*3)) = last_window;
  (*(M_Object*)(o->data + SZ_INT)) = last_window->last_widget;
  if(last_window->last_widget && strcmp(o->type_ref->name, "Layout")
    && strcmp(o->type_ref->name, "Menu"))
    widget_add(LIST(last_window->last_widget), o);
}

static m_uint o_nk_align;
static CTOR(sval_ctor)
{
  *(m_uint*)(o->data + o_nk_align) = NK_TEXT_LEFT;
}
static m_uint o_nk_wrap, o_nk_labelcolor;
static void label_execute(M_Object o, struct nk_context* ctx)
{
  if(*(M_Object*)(o->data + o_nk_labelcolor))
  {
    M_Object color = *(M_Object*)(o->data + o_nk_labelcolor);
    if(*(m_uint*)(o->data + o_nk_wrap))
      nk_label_colored_wrap(ctx, NAME(o), COLOR(color));           
    else
      nk_label_colored(ctx, NAME(o), *(m_uint*)(o->data + o_nk_align), COLOR(color));
  }
  else
  {
    if(*(m_uint*)(o->data + o_nk_wrap))
      nk_label_wrap(ctx, NAME(o));           
    else
      nk_label(ctx, NAME(o), *(m_uint*)(o->data + o_nk_align));
  }
}
static CTOR(label_ctor)
{
  *(f_nk*)(o->data + SZ_INT*2) = label_execute;
}

static void text_execute(M_Object o, struct nk_context* ctx)
{
  if(*(m_uint*)(o->data + o_nk_wrap))
    nk_text_wrap(ctx, NAME(o), strlen(NAME(o)));           
  else
    nk_text(ctx, NAME(o), strlen(NAME(o)), *(m_uint*)(o->data + o_nk_align));           
}
static CTOR(text_ctor)
{
  *(f_nk*)(o->data + SZ_INT*2) = text_execute;
}

static m_uint o_nk_select;
static void slabel_execute(M_Object o, struct nk_context* ctx)
{
  if(nk_selectable_label(ctx, NAME(o), *(m_uint*)(o->data + o_nk_align), (int*)&*(m_uint*)(o->data + o_nk_select)))
    broadcast(o);
}
static CTOR(slabel_ctor)
{
  *(f_nk*)(o->data + SZ_INT*2) = slabel_execute;
}

static void stext_execute(M_Object o, struct nk_context* ctx)
{
  if(nk_selectable_text(ctx, NAME(o), strlen(NAME(o)), *(m_uint*)(o->data + o_nk_align), (int*)&*(m_uint*)(o->data + o_nk_select)))
    broadcast(o);
}
static CTOR(stext_ctor)
{
  *(f_nk*)(o->data + SZ_INT*2) = stext_execute;
}

static m_int o_nk_prog, o_nk_progmax, o_nk_progmod;
static void prog_execute(M_Object o, struct nk_context* ctx)
{
  if(nk_progress(ctx, (m_uint*)(o->data + o_nk_prog), *(m_uint*)(o->data + o_nk_progmax), *(m_uint*)(o->data + o_nk_progmod)))
   broadcast(o);
}
static CTOR(prog_ctor)
{
  *(f_nk*)(o->data + SZ_INT*2) = prog_execute;
  *(m_uint*)(o->data + o_nk_prog) = 0;
  *(m_uint*)(o->data + o_nk_progmax) = 100;
  *(m_uint*)(o->data + o_nk_progmod) = 1;
}


static m_int o_nk_behavior, o_nk_button_color;
static void button_execute(M_Object o, struct nk_context* ctx)
{
  if(*(m_uint*)(o->data + o_nk_button_color))
  {
  M_Object color = *(M_Object*)(o->data + o_nk_button_color);
  if(nk_button_color(ctx, COLOR(color)))
      broadcast(o);
  } else if(NAME(o)) {
      if(nk_button_label(ctx, NAME(o)))
        broadcast(o);
  }
}

static CTOR(button_ctor) {
  *(f_nk*)(o->data + SZ_INT*2) = button_execute;
}

static void group_exec(M_Object o, struct nk_context* ctx) {
  Vector v = LIST(o);
  for(m_uint i = 0; i < vector_size(v); i++) {
    const M_Object obj = (M_Object)vector_at(v, i);
    const f_nk exec = *(f_nk*)(obj->data + SZ_INT*2);
    exec(obj, ctx);
  }
}
static CTOR(group_ctor) {
  LIST(o)= new_vector(shred->info->mp);
  last_window->last_widget = o;
}

static DTOR(group_dtor) {
  free_vector(shred->info->mp, LIST(o));
  last_window->last_widget = *(M_Object*)(o->data + SZ_INT);
}

static MFUN(group_end) {
  last_window->last_widget = *(M_Object*)(o->data + SZ_INT);
}

static MFUN(group_begin) {
  last_window->last_widget = o;
}

static m_int o_nk_rowcol;
static m_int o_nk_rowh;
static m_int o_nk_roww;
static m_int o_nk_static;
static void row(M_Object o, struct nk_context* ctx) {
  if(*(m_uint*)(o->data + o_nk_static))
    nk_layout_row_static(ctx, *(m_uint*)(o->data + o_nk_rowh),
      *(m_uint*)(o->data + o_nk_roww), *(m_uint*)(o->data + o_nk_rowcol));
  else
    nk_layout_row_dynamic(ctx, *(m_uint*)(o->data + o_nk_rowh),
      *(m_uint*)(o->data + o_nk_rowcol));
}

static void rowd_execute(M_Object o, struct nk_context* ctx) {
  row(o, ctx);
  group_exec(o, ctx);
}

static CTOR(rowd_ctor) {
  (*(f_nk*)(o->data + SZ_INT*2)) = rowd_execute;
  *(m_uint*)(o->data + o_nk_rowcol) = 1;
  *(m_uint*)(o->data + o_nk_rowh) = 30;
  *(m_uint*)(o->data + o_nk_roww) = 100;
}

static m_int o_nk_x;
static m_int o_nk_y;
static m_int o_nk_w;
static m_int o_nk_h;
static m_int o_nk_flags;
static void layout_execute(M_Object o, struct nk_context* ctx) {
  char name[256];
  sprintf(name, "%p", o);
  if (nk_begin_titled(ctx, name,  NAME(o),
      nk_rect(*(m_uint*)(o->data + o_nk_x),
        *(m_uint*)(o->data + o_nk_y),
        *(m_uint*)(o->data + o_nk_w),
        *(m_uint*)(o->data + o_nk_h)),
        *(m_uint*)(o->data + o_nk_flags))) {
    row(o, ctx);
    group_exec(o, ctx);
  }
  nk_end(ctx);
}

static CTOR(layout_ctor) {
  (*(f_nk*)(o->data + SZ_INT*2))    = layout_execute;
  (*(m_uint*)(o->data + o_nk_x))     = 50;
  (*(m_uint*)(o->data + o_nk_y))     = 50;
  (*(m_uint*)(o->data + o_nk_w))     = 400;
  (*(m_uint*)(o->data + o_nk_h))     = 400;
  (*(m_uint*)(o->data + o_nk_flags)) =
    NK_WINDOW_BORDER|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE|
    NK_WINDOW_CLOSABLE|NK_WINDOW_MINIMIZABLE|NK_WINDOW_TITLE;
  if(last_window) {
    M_Object obj = (M_Object)vector_back(last_window->widget);
    if(obj)
      widget_add(last_window->widget, o);
  }
}

static m_int o_nk_state;
static void tree_execute(M_Object o, struct nk_context* ctx) {
  if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT))
    broadcast(o);
  int i = 0;
  if(nk_tree_push_id(ctx, *(m_uint*)(o->data + o_nk_state), NAME(o), i, (m_uint)o)) {
    row(o, ctx);
    group_exec(o, ctx);
    nk_tree_pop(ctx);
  }
}

static CTOR(tree_ctor) {
  (*(f_nk*)(o->data + SZ_INT*2)) = tree_execute;
  (*(m_uint*)(o->data + o_nk_state)) = NK_TREE_TAB;
}

static m_int o_nk_comboval;
static void combo_execute(M_Object o, struct nk_context* ctx) {
  m_uint i;
  struct nk_vec2 vec = {200.0, 200.0};
  if(nk_combo_begin_label(ctx, NAME(o), vec)) {
    row(o, ctx);
    for(i = 0; i < vector_size(LIST(o)); i++) {
      if(nk_combo_item_label(ctx, STRING(vector_at(LIST(o), i)), NK_TEXT_LEFT)) {
        *(m_uint*)(o->data + o_nk_comboval) = i;
        broadcast(o);
        break;
      }
    }
    nk_combo_end(ctx);
  }
}

static CTOR(combo_ctor) {
  *(f_nk*)(o->data + SZ_INT*2) = combo_execute;
}

static MFUN(combo_add) {
  widget_add(LIST(o), *(M_Object*)(shred->mem + SZ_INT));
  *(m_uint*)RETURN = (m_uint)*(M_Object*)(shred->mem + SZ_INT);
}

static void menubar_execute(M_Object o, struct nk_context* ctx) {
  nk_menubar_begin(ctx);
  group_exec(o, ctx);
  nk_menubar_end(ctx);
}

static CTOR(menubar_ctor) {
  *(f_nk*)(o->data + SZ_INT*2) = menubar_execute;
}

static MFUN(menubar_add) {
  widget_add(LIST(o), *(M_Object*)(shred->mem + SZ_INT));
  *(m_uint*)RETURN = (m_uint)*(M_Object*)(shred->mem + SZ_INT);
}

static m_int  o_nk_menuval;
static void menu_execute(M_Object o, struct nk_context* ctx) {
  m_uint i;
  struct nk_vec2 vec = {200.0, 200.0};
  if(nk_menu_begin_label(ctx, NAME(o), NK_TEXT_LEFT, vec)) {
    for(i = 0; i < vector_size(LIST(o)); i++) {
      row(o, ctx);
      if(nk_menu_item_label(ctx, STRING(vector_at(LIST(o), i)), NK_TEXT_LEFT)) {
        *(m_uint*)(o->data + o_nk_comboval) = i;
        broadcast(o);
        break;
      }
    }
    nk_menu_end(ctx);
  }
}

static CTOR(menu_ctor) {
  *(f_nk*)(o->data + SZ_INT*2) = menu_execute;
}

static MFUN(menu_add) {
  widget_add(LIST(o), *(M_Object*)(shred->mem + SZ_INT));
  *(m_uint*)RETURN = (m_uint)*(M_Object*)(shred->mem + SZ_INT);
}

static m_int o_nk_edit_type;
static void nkstring_execute(M_Object o, struct nk_context* ctx) {
  char c[512];
  memset(c, 0, 512);
  strcpy(c, NAME(o));
  nk_layout_row_static(ctx, 180, 278, 1);
  nk_edit_string_zero_terminated(ctx, *(m_uint*)(o->data + o_nk_edit_type), c, 512, nk_filter_default);           
  NAME(o) = strdup(c);
}

static CTOR(nkstring_ctor) {
  *(f_nk*)(o->data + SZ_INT*2) = nkstring_execute;
  *(m_uint*)(o->data + o_nk_edit_type) = NK_EDIT_SIMPLE;
/*  *(m_uint*)(o->data + o_nk_edit_type) = NK_EDIT_BOX;*/
}

static void check_execute(M_Object o, struct nk_context* ctx) {
  if(nk_checkbox_label(ctx, NAME(o), (int*)&*(m_int*)(o->data + o_nk_ival)))
    broadcast(o);
}

static CTOR(check_ctor) {
  (*(f_nk*)(o->data + SZ_INT*2)) = check_execute;
}

static void propi_execute(M_Object o, struct nk_context* ctx) {
  m_int property = *(m_int*)(o->data + o_nk_ival);
  nk_property_int(ctx, NAME(o), *(m_int*)(o->data + o_nk_imin), (int*)&*(m_int*)(o->data + o_nk_ival),
    *(m_int*)(o->data + o_nk_imax), *(m_int*)(o->data + o_nk_istp), *(m_float*)(o->data + o_nk_iinc));
  if(property != *(m_int*)(o->data + o_nk_ival))
    broadcast(o);
}

static CTOR(propi_ctor) {
  (*(f_nk*)(o->data + SZ_INT*2)) = propi_execute;
  *(m_int*)(o->data + o_nk_imax) = 100;
  *(m_int*)(o->data + o_nk_istp) = 10;
  *(m_float*)(o->data + o_nk_iinc) = 1.0;
}

static void slideri_execute(M_Object o, struct nk_context* ctx) {
  if(nk_slider_int(ctx, *(m_int*)(o->data + o_nk_imin), (int*)&*(m_int*)(o->data + o_nk_ival),
    *(m_int*)(o->data + o_nk_imax), *(m_int*)(o->data + o_nk_istp)))
    broadcast(o);
}

static CTOR(slideri_ctor) {
  (*(f_nk*)(o->data + SZ_INT*2)) = slideri_execute;
  *(m_int*)(o->data + o_nk_imax) = 1.0;
  *(m_int*)(o->data + o_nk_ival) = 0.0;
  *((m_int*)((M_Object)o)->data + o_nk_istp) = 0.1; // ? BUG
}

static void propf_execute(M_Object o, struct nk_context* ctx) {
  m_float property = *(m_float*)(o->data + o_nk_fval);
#ifdef USE_DOUBLE
  nk_property_double(ctx, NAME(o), *(m_float*)(o->data + o_nk_fmin), &*(m_float*)(o->data + o_nk_fval),
#else
  nk_property_float(ctx, NAME(o), *(m_float*)(o->data + o_nk_fmin), &*(m_float*)(o->data + o_nk_fval),
#endif
    *(m_float*)(o->data + o_nk_fmax), 0.1, *(m_float*)(o->data + o_nk_finc));
  if(property != *(m_float*)(o->data + o_nk_fval))
    broadcast(o);
}

static CTOR(propf_ctor) {
  (*(f_nk*)(o->data + SZ_INT*2)) = propf_execute;
  *(m_float*)(o->data + o_nk_fmax) = 1.0;
  *(m_float*)(o->data + o_nk_fval) = 0.0;
  *(m_float*)(o->data + o_nk_fstp) = 1;
  *(m_float*)(o->data + o_nk_finc) = .5;
}

static void slider_execute(M_Object o, struct nk_context* ctx) {
  if(nk_slider_float(ctx, *(m_float*)(o->data + o_nk_fmin), (float*)&*(m_float*)(o->data + o_nk_fval),
    *(m_float*)(o->data + o_nk_fmax), .01))
    /*if(nk_slider_float(ctx, *(m_float*)(o->data + o_nk_fmin), (float*)&*(m_float*)(o->data + o_nk_fval),*/
    /**(m_float*)(o->data + o_nk_fmax), *((m_float*)((M_Object)o)->d.data + o_nk_fstp)))*/
    broadcast(o);
}

static CTOR(slider_ctor) {
  (*(f_nk*)   (o->data + SZ_INT*2)) = slider_execute;
  *(m_float*)(o->data + o_nk_fmax)  = 1.0;
  *(m_float*)(o->data + o_nk_fval)  = 0.0;
  *(m_float*)(o->data + o_nk_fstp)  = 0.1; // ? BUG
  *(m_float*)(o->data + o_nk_finc)  = .11;
}

GWION_IMPORT(Nuklear) {
  GWI_B(gwi_struct_ini(gwi, "Nk"))

  GWI_B(gwi_class_ini(gwi, "Color", NULL))
  gwi_item_ini(gwi,"int", "r");
  gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"int", "g");
  gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"int", "b");
  gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"int", "a");
  gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_B(gwi_class_end(gwi))

  DECL_B(const Type, t_panel, = gwi_class_ini(gwi, "Panel", NULL));
  gwi_class_xtor(gwi, nk_ctor, nk_dtor);
  t_panel->nspc->offset += SZ_INT;
  GWI_B(gwi_class_end(gwi))

  DECL_B(const Type, t_widget, = gwi_class_ini(gwi, "Widget", "Event"));
  gwi_class_xtor(gwi, widget_ctor, NULL);
  t_widget->nspc->offset += SZ_INT*3;
  gwi_item_ini(gwi,"string", "name");
  o_nk_name = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Sval", "Widget"))
  gwi_class_xtor(gwi, sval_ctor, NULL);
  gwi_item_ini(gwi, "int",  "align");
  o_nk_align = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Label", "Sval"))
  gwi_class_xtor(gwi, label_ctor, NULL);
  gwi_item_ini(gwi, "int",  "wrap");
  o_nk_wrap = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "Color",  "color");
  o_nk_labelcolor= gwi_item_end(gwi, ae_flag_late, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Text", "Sval"))
  gwi_class_xtor(gwi, text_ctor, NULL);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Stext", "Sval"))
  gwi_class_xtor(gwi, stext_ctor, NULL);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "SLabel", "Sval"))
  gwi_class_xtor(gwi, slabel_ctor, NULL);
  gwi_item_ini(gwi, "int",  "selectable");
  o_nk_select = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Prog", "Widget"))
  gwi_class_xtor(gwi, prog_ctor, NULL);
  gwi_item_ini(gwi,"int", "val");
  o_nk_prog = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"int", "max");
  o_nk_progmax = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"int", "mod");
  o_nk_progmod = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Button", "Widget"))
  gwi_class_xtor(gwi, button_ctor, NULL);
  gwi_item_ini(gwi, "int",  "behavior");
  o_nk_behavior = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "Color",  "color");
  o_nk_button_color= gwi_item_end(gwi, ae_flag_late, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Group", "Widget"))
  gwi_class_xtor(gwi, group_ctor, group_dtor);
  gwi_item_ini(gwi, "int", "widget");
  o_nk_list = gwi_item_end(gwi, ae_flag_private, num, 0);
  gwi_func_ini(gwi, "void", "begin");
  GWI_B(gwi_func_end(gwi, group_begin, ae_flag_none))
  gwi_func_ini(gwi, "void", "end");
  GWI_B(gwi_func_end(gwi, group_end, ae_flag_none))
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Row", "Group"))
  gwi_class_xtor(gwi, rowd_ctor, NULL);
  gwi_item_ini(gwi, "int",  "height");
  o_nk_rowh = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "int",  "width");
  o_nk_roww = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "int",  "col");
  o_nk_rowcol = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "int",  "static");
  o_nk_static = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Layout", "Row"))
  gwi_class_xtor(gwi, layout_ctor, NULL);
  gwi_item_ini(gwi, "int",  "x");
  o_nk_x = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "int",  "y");
  o_nk_y = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "int",  "w");
  o_nk_w = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "int",  "h");
  o_nk_h = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "int",  "flag");
  o_nk_flags = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"int", "BORDER");
  gwi_item_end(gwi, ae_flag_static | ae_flag_const, num, NK_WINDOW_BORDER);
  gwi_item_ini(gwi,"int", "MOVABLE");
  gwi_item_end(gwi, ae_flag_static | ae_flag_const, num, NK_WINDOW_MOVABLE);
  gwi_item_ini(gwi,"int", "SCALABLE");
  gwi_item_end(gwi, ae_flag_static | ae_flag_const, num, NK_WINDOW_SCALABLE);
  gwi_item_ini(gwi,"int", "CLOSABLE");
  gwi_item_end(gwi, ae_flag_static | ae_flag_const, num, NK_WINDOW_CLOSABLE);
  gwi_item_ini(gwi,"int", "MINIMIZABLE");
  gwi_item_end(gwi, ae_flag_static | ae_flag_const, num, NK_WINDOW_MINIMIZABLE);
  gwi_item_ini(gwi,"int", "TITLE");
  gwi_item_end(gwi, ae_flag_static | ae_flag_const, num, NK_WINDOW_TITLE);
  gwi_item_ini(gwi,"int", "MENU");
  gwi_item_end(gwi, ae_flag_static | ae_flag_const, num, NK_PANEL_MENU);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Combo", "Row"))
  gwi_class_xtor(gwi, combo_ctor, NULL);
  gwi_item_ini(gwi, "int",  "val");
  o_nk_comboval = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_func_ini(gwi, "string", "add");
  gwi_func_arg(gwi, "string", "s");
  GWI_B(gwi_func_end(gwi, combo_add, ae_flag_none))
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Menu", "Group"))
  gwi_class_xtor(gwi, menu_ctor, NULL);
  gwi_item_ini(gwi, "int",  "val");
  o_nk_menuval = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_func_ini(gwi, "string", "add");
  gwi_func_arg(gwi, "string", "s");
  GWI_B(gwi_func_end(gwi, menu_add, ae_flag_none))
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "MenuBar", "Group"))
  gwi_class_xtor(gwi, menubar_ctor, NULL);
  gwi_func_ini(gwi, "void", "add");
  gwi_func_arg(gwi, "Menu", "s");
  GWI_B(gwi_func_end(gwi, menubar_add, ae_flag_none))
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Tree", "Row"))
  gwi_class_xtor(gwi, tree_ctor, NULL);
  gwi_item_ini(gwi, "int",  "state");
  o_nk_state = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "String", "Row"))
  gwi_class_xtor(gwi, nkstring_ctor, NULL);
  gwi_item_ini(gwi,"int", "type");
  o_nk_edit_type= gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"int", "SIMPLE");
  gwi_item_end(gwi, ae_flag_static | ae_flag_const, num, NK_EDIT_SIMPLE);
  gwi_item_ini(gwi,"int", "FIELD");
  gwi_item_end(gwi,  ae_flag_static | ae_flag_const, num, NK_EDIT_FIELD);
  gwi_item_ini(gwi,"int", "BOX");
  gwi_item_end(gwi,    ae_flag_static | ae_flag_const, num, NK_EDIT_BOX);
  gwi_item_ini(gwi,"int", "EDITOR");
  gwi_item_end(gwi, ae_flag_static | ae_flag_const, num, NK_EDIT_EDITOR);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Ival", "Widget"))
  gwi_item_ini(gwi, "int",  "val");
  o_nk_ival = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "int",  "min");
  o_nk_imin = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "int",  "max");
  o_nk_imax = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"int", "step");
  o_nk_istp = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"float", "inc");
  o_nk_iinc = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Propi", "Ival"))
  gwi_class_xtor(gwi, propi_ctor, NULL);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "SliderI", "Ival"))
  gwi_class_xtor(gwi, slideri_ctor, NULL);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Check", "Ival"))
  gwi_class_xtor(gwi, check_ctor, NULL);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Fval", "Widget"))
  gwi_item_ini(gwi, "float",  "val");
  o_nk_fval = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "float",  "min");
  o_nk_fmin = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi, "float",  "max");
  o_nk_fmax = gwi_item_end(gwi, ae_flag_none, num, 0);

  gwi_item_ini(gwi,"float", "step");
  o_nk_fstp = gwi_item_end(gwi, ae_flag_none, num, 0);
  gwi_item_ini(gwi,"float", "inc");
  o_nk_finc = gwi_item_end(gwi, ae_flag_none, num, 0);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "PropF", "Fval"))
  gwi_class_xtor(gwi, propf_ctor, NULL);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_class_ini(gwi, "Slider", "Fval"))
  gwi_class_xtor(gwi, slider_ctor, NULL);
  GWI_B(gwi_class_end(gwi))

  GWI_B(gwi_struct_end(gwi))
  return true;
}
