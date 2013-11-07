#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <Elementary.h>
#include "Edje_Pick.h"

#define CLIENT_NAME         "Edje-Pick Client"

#define EDJE_PICK_DRAG_TIMEOUT 0.3
#define EDJE_PICK_ANIM_TIME 0.5

#define EDJE_PICK_SYS_DND_PREFIX  "file://"
#define EDJE_PICK_SYS_DND_POSTFIX "></item>"
#define EDJE_PICK_DND_DELIM  ':'

#define EDJE_PICK_GROUPS_STR  "Groups"
#define EDJE_PICK_IMAGES_STR  "Images"
#define EDJE_PICK_SAMPLES_STR "Samples"
#define EDJE_PICK_FONTS_STR   "Fonts"

#define EDJE_PICK_NEW_FILE_NAME_STR "Untitled"

#define EDJE_PICK_PREVIEW_TIMEOUT 0.5
#define EDJE_PICK_PREVIEW_ANIM 0.05

static Eina_Bool _image_preview_timeout(void *data);
static Eina_Bool _sample_preview_timeout(void *data);

enum _Edje_Pick_Type
{
   EDJE_PICK_TYPE_UNDEF,  /* 0 - if not intialized */
   EDJE_PICK_TYPE_FILE,   /* Node is file head */
   EDJE_PICK_TYPE_LIST,   /* Node contains list of "Groups", "Images", ... */
   EDJE_PICK_TYPE_GROUP,  /* Node contains a group name */
   EDJE_PICK_TYPE_IMAGE,  /* Node contains a image name */
   EDJE_PICK_TYPE_SAMPLE, /* Node contains a sound-sample name */
   EDJE_PICK_TYPE_FONT,   /* Node contains a font name */
   EDJE_PICK_TYPE_LAST    /* UNUSED, Marking Last */
};
typedef enum _Edje_Pick_Type Edje_Pick_Type;

enum _Edje_Pick_Image_Preview_Status
{
   EDJE_PICK_PRE_IMG_START,    /* Preview window appears         */
   EDJE_PICK_PRE_IMG_BLOW,     /* Preview window grows in size   */
   EDJE_PICK_PRE_IMG_SHRINK,   /* Preview window shrinks in size */
   EDJE_PICK_PRE_IMG_STABLE,   /* Preview window at max-size     */
   EDJE_PICK_PRE_IMG_END,      /* Preview window closed          */
   EDJE_PICK_PRE_IMG_DETACHED  /* Preview window was detached    */
};
typedef enum _Edje_Pick_Image_Preview_Status Edje_Pick_Image_Preview_Status;

enum _Edje_Pick_Sample_Preview_Status
{
   EDJE_PICK_PRE_SMP_START,
   EDJE_PICK_PRE_SMP_COLOR_INC,  /* Change icon color before sample plays */
   EDJE_PICK_PRE_SMP_COLOR_DEC   /* Change icon color before sample stops */
};
typedef enum _Edje_Pick_Sample_Preview_Status Edje_Pick_Sample_Preview_Status;

struct _image_preview_st
{
   Ecore_Timer *tm;
   Evas_Object *icon;    /* Pointer to original icon, used on create only */
   Evas_Object *ic;      /* Preview icon in bt */
   Evas_Object *bt;
   Edje_Pick_Image_Preview_Status status;
   double min_size;
   unsigned int x;       /* Center point of preview window (x) */
   unsigned int y;       /* Center point of preview window (y) */
   unsigned int w;       /* Width of target raw-image  */
   unsigned int h;       /* Hieght of target raw-image */
};
typedef struct _image_preview_st image_preview_st;

struct _sample_preview_st
{
   Edje_Pick_Sample_Preview_Status status;
   Ecore_Timer *tm;
   Evas_Object *icon;    /* Pointer to original icon */
   void *sample;
   int size;

#define INI_R 60
#define INI_G 66
#define INI_B 64
#define INI_A 128
#define COLOR_STEP 4
   int steps;            /* Steps of color-incs */
};
typedef struct _sample_preview_st sample_preview_st;

/* gl info as follows:
   file_name = full-path file name for all nodes.
   if its genlist-group data struct will contain:
   name = full-path file name.
   sub = list items named: "Groups", "Images", "Samples", "Fonts"

   For each group will have a node both its name and sub-items list.
   For each sub-item just fill-in name and set sub to NULL */
struct _gl_item_info
{
   const char *file_name;   /* From what file it comes */
   Edje_Pick_Type type;     /* Specifys what type of data struct contains */
   const char *name;        /* Item name to display */
   void *ex;                /* Extra info for this item */
   void *preview;           /* Will point to a preview struct if alloc */
   Eina_List *sub;          /* Not NULL for item represent head of tree */
   Eina_List *r;            /* Replacement (used by Image, Sample, Font) */
};
typedef struct _gl_item_info gl_item_info;

struct _action_st
{  /* Struct used to implement UNDO, REDO */
   Evas_Object *gl_src;    /* From what GL it was taken */
   Evas_Object *gl_dst;    /* To what GL it was taken moved */
   Eina_List *list;        /* List of gl_item_info for action */
};
typedef struct _action_st action_st;

struct _gl_actions
{  /* For UNDO, REDO */
   Elm_Object_Item *take_bt;
   Elm_Object_Item *remove_bt;
   Elm_Object_Item *undo_bt;
   Elm_Object_Item *redo_bt;
   Eina_List *act; /* List of action_st structs */
   unsigned int c; /* Current */
};
typedef struct _gl_actions gl_actions;

struct _gui_elements
{  /* Main window elements */
   Evas_Object *win;
   Evas_Object *tb;
   Evas_Object *bx;
   Evas_Object *popup;
   Evas_Object *inwin;  /* Holds file selector */
   const char *file_to_open;
   const char *file_name;
   Eina_Bool modified; /* Flags that file modified */

   /* Pane elements */
   Evas_Object *panes;
   Evas_Object *bx_left;
   Evas_Object *bx_right;

   /* Menu Items */
   Elm_Object_Item *menu_open;
   Elm_Object_Item *menu_close;
   Elm_Object_Item *menu_save;
   Elm_Object_Item *menu_save_as;
   Elm_Object_Item *menu_quit;

   /* Genlist and global data */
   const char *argv0;  /* Program name */
   Evas_Object *gl_src;
   Evas_Object *gl_dst;
   Elm_Genlist_Item_Class itc;
   Elm_Genlist_Item_Class itc_group;
   Edje_Pick *context;

   gl_actions actions;  /* For UNDO, REDO */
};
typedef struct _gui_elements gui_elements;

static void
_image_preview_close(image_preview_st *st)
{
   if (st->tm)
     ecore_timer_del(st->tm);

   if (st->ic)
     evas_object_del(st->ic);

   if (st->bt)
     evas_object_del(st->bt);

   st->tm = st->ic = st->bt = NULL;
}

static Eina_Bool
_play_sample_finished(void *data EINA_UNUSED,
      void *in EINA_UNUSED,
      const void *desc EINA_UNUSED,
      void *event_info EINA_UNUSED)
{  /* TODO: Will be used later to manage sample-playing */
   printf("<%s> <%d>\n", __func__, __LINE__);
   return EINA_TRUE;
}

static void
_sample_preview_stop(sample_preview_st *st)
{  /* TODO: Cannot stop sample that played at the moment */
   if (st->tm)
     ecore_timer_del(st->tm);

   st->tm = NULL;
   st->status = EDJE_PICK_PRE_IMG_START;
}

static void *
_gl_item_preview_free(void *p, Edje_Pick_Type type)
{
   if (p)
     {
        switch(type)
          {
           case EDJE_PICK_TYPE_SAMPLE:
                {
                   break;
                }

           case EDJE_PICK_TYPE_IMAGE:
                {
                   _image_preview_close(p);
                   break;
                }

           default:
              break;
          }

        free(p);
     }

   return NULL;
}

static void *
_gl_item_ex_free(void *ex, Edje_Pick_Type type)
{
   if (ex)
     {
        switch(type)
          {
           case EDJE_PICK_TYPE_SAMPLE:
           case EDJE_PICK_TYPE_IMAGE:
                {
                   break;
                }

           default:
             break;
          }

        free(ex);
     }

   return NULL;
}

static void
_gl_item_data_free(gl_item_info *info)
{  /* Free genlist item info, and remove from head list or free sub-items */
   if (!info)
     return;

   gl_item_info *ptr;
   EINA_LIST_FREE(info->sub, ptr)
     {  /* Loop to free and sub-info */
        if (ptr->sub)
          _gl_item_data_free(ptr);
        else
          {
             eina_stringshare_del(ptr->file_name);
             eina_stringshare_del(ptr->name);
             free(ptr);
          }
     }

   info->preview = _gl_item_preview_free(info->preview, info->type);
   info->ex = _gl_item_ex_free(info->ex, info->type);
   eina_stringshare_del(info->file_name);
   eina_stringshare_del(info->name);
   free(info);
}

static void
_gl_data_free(Evas_Object *gl)
{
   Elm_Object_Item *glit = elm_genlist_first_item_get(gl);
   while(glit)
     {
        gl_item_info *tmp;
        tmp = elm_object_item_data_get(glit);
        if ((tmp->type == EDJE_PICK_TYPE_FILE) ||
              (tmp->type == EDJE_PICK_TYPE_LIST))
          {
             _gl_item_data_free(tmp);
             elm_object_item_del(glit);
             glit = elm_genlist_first_item_get(gl);
          }
        else
          glit = elm_genlist_item_next_get(glit);
     }
}

static void
_actions_list_clear(gl_actions *a)
{
   action_st *st;

   EINA_LIST_FREE(a->act, st)
     {
        gl_item_info *i;
        EINA_LIST_FREE(st->list, i)
          {
             eina_list_free(i->r);
             i->r = NULL;
          }

        free(st);
     }

   a->c = 0;
   elm_object_item_disabled_set(a->take_bt, EINA_TRUE);
   elm_object_item_disabled_set(a->remove_bt, EINA_TRUE);
   elm_object_item_disabled_set(a->redo_bt, EINA_TRUE);
   elm_object_item_disabled_set(a->undo_bt, EINA_TRUE);
}

gui_elements *
_gui_alloc(void)
{  /* Will do any complex-allocation proc here */
   gui_elements *g = calloc(1, sizeof(gui_elements));
   g->context = edje_pick_context_new();
   return g;
}

static void
_actions_list_add(gl_actions *a, Eina_List *infos,
      Evas_Object *src, Evas_Object *dst)
{
   if (infos)
     {
        action_st *st = calloc(1, sizeof(*st));
        st->list = infos;
        st->gl_src = src;
        st->gl_dst = dst;

          {  /* Before append, we trucate any actions after act[c] location */
             action_st *t = eina_list_nth(a->act, a->c);
             while (t)
               {
                  gl_item_info *i;
                  EINA_LIST_FREE(t->list, i)
                    {
                       eina_list_free(i->r);
                       i->r = NULL;
                    }

                  free(t);
                  a->act = eina_list_remove(a->act, t);
                  t = eina_list_nth(a->act, a->c);
               }
          }

        a->act = eina_list_append(a->act, st);
        a->c = eina_list_count(a->act);
     }

   elm_object_item_disabled_set(a->redo_bt, eina_list_count(a->act));
   elm_object_item_disabled_set(a->undo_bt, (a->c == 0));
}

static void
_gui_free(gui_elements *g)
{
   if (g->file_to_open)
     eina_stringshare_del(g->file_to_open);

   if (g->file_name)
     eina_stringshare_del(g->file_name);

   elm_drag_item_container_del(g->gl_src);
   elm_drop_item_container_del(g->gl_src);

   elm_drag_item_container_del(g->gl_dst);
   elm_drop_item_container_del(g->gl_dst);

   _gl_data_free(g->gl_src);
   _gl_data_free(g->gl_dst);

   _actions_list_clear(&(g->actions));

   edje_pick_context_free(g->context);
   free(g);
}

static int
_item_name_cmp(const void *d1, const void *d2)
{
   return (strcmp(((gl_item_info *) d1)->name, d2));
}

static int
_item_ptr_cmp(const void *d1, const void *d2)
{
   return (d1 - d2);
}

static int
_item_type_cmp(const void *d1, const void *d2)
{  /* Sort so FONT first - FILE last */
   return (((gl_item_info *) d1)->type - ((Edje_Pick_Type) d2));
}

/* START - Genlist Item handling */
static char *
_group_item_text_get(void *data, Evas_Object *obj EINA_UNUSED,
      const char *part EINA_UNUSED)
{
   gl_item_info *info = data;
   char *s = malloc(strlen(info->name) + 2);

   sprintf(s, "%s%c",info->name, (0/*info->append*/) ? '*' : '\0');
   return s;
}

static Eina_Bool
_edje_pick_image_object_data_read(Evas_Object *o,
      image_info_ex *ex, const char *file_name,
      unsigned int *w, unsigned int *h)
{
   void *img;
   int alpha;
   int compression;
   int quality;
   int lossy;
   Eet_File *ef = eet_open(file_name, EET_FILE_MODE_READ);

   if (ef)
     {
        char buf[1024];
        snprintf(buf, sizeof(buf), "edje/images/%i", ex->id);
        img = eet_data_image_read(ef,
              buf,
              w,
              h,
              &alpha,
              &compression,
              &quality,
              &lossy);

        if (img)
          {
             evas_object_size_hint_min_set(o, *w, *h);
             evas_object_image_colorspace_set(o, EVAS_COLORSPACE_ARGB8888);
             evas_object_image_alpha_set(o, EINA_FALSE);
             evas_object_image_size_set(o, *w, *h);
             evas_object_image_data_copy_set(o, img);
             evas_object_image_data_update_add(o, 0, 0, *w, *h);
             evas_object_show(o);
             return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static void
_group_preview_open(gl_item_info *info)
{  /* Open Group preview */
   Evas_Object *win = elm_win_util_standard_add("Group Preview", info->name);
   Evas_Object *ly = elm_layout_add(win);
   elm_layout_file_set(ly, info->file_name, info->name);
   evas_object_size_hint_weight_set(ly, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(ly, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_layout_sizing_eval(ly);
   elm_win_resize_object_add(win, ly);
   elm_win_autodel_set(win, EINA_TRUE);
   evas_object_show(ly);
   evas_object_show(win);
}

static void
_preview_win_del(void *data,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{  /* called when client window is deleted */
   gl_item_info *info = data;
   image_preview_st *st = info->preview;
   st->status = EDJE_PICK_PRE_IMG_START;
}

static void
_preview_open(gl_item_info *info)
{  /* Open full-blown image-preview window */
   unsigned int w, h;
   image_preview_st *st = info->preview;
   st->status = EDJE_PICK_PRE_IMG_DETACHED;
   Evas_Object *win = elm_win_util_standard_add("Image Preview", info->name);

   Evas_Object *im = elm_image_add(win);
   Evas_Object *img = elm_image_object_get(im);
   if (_edje_pick_image_object_data_read(img, info->ex,
            info->file_name, &w, &h))

   elm_win_autodel_set(win, EINA_TRUE);
   evas_object_smart_callback_add(win, "delete,request", _preview_win_del, info);

   evas_object_size_hint_weight_set(im, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(im, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_min_set(im, w, h);
   elm_win_resize_object_add(win, im);
   evas_object_resize(win, w, h);

   evas_object_show(img);
   evas_object_show(im);
   evas_object_show(win);
}

static void
_preview_clicked(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   gl_item_info *info = data;
   _image_preview_close(info->preview);
   _preview_open(info);
}

static void
_preview_mouse_out_cb(void *data,
               Evas *e __UNUSED__,
               Evas_Object *obj __UNUSED__,
               void *event_info __UNUSED__)
{
   gl_item_info *info = data;
   image_preview_st *st = info->preview;
   if (st->status != EDJE_PICK_PRE_IMG_DETACHED)
     {
        st->status = EDJE_PICK_PRE_IMG_SHRINK;
        st->tm = ecore_timer_add(EDJE_PICK_PREVIEW_ANIM,
              _image_preview_timeout, info);
     }
}

static void
_show_image_preview(gl_item_info *info)
{
   image_preview_st *st = info->preview;
   Evas_Coord x, y, w, h;
   st->bt = elm_button_add(elm_object_top_widget_get(st->icon));
   st->ic = elm_icon_add(elm_object_top_widget_get(st->icon));
   evas_object_image_source_set(elm_image_object_get(st->ic),
         elm_image_object_get(st->icon));

   elm_object_style_set(st->bt, "anchor");
   elm_object_part_content_set(st->bt, "icon", st->ic);

   evas_object_geometry_get(st->icon, &x, &y, &w, &h);
   st->min_size = (st->w > st->h) ? (((double) (w << 1)) / st->w) :
      (((double) (h << 1)) / st->h);
   /* Compute icon center point */
   st->x = x + (w >> 1);
   st->y = y + (h >> 1);

   /* Keep current icon size */
   w = (st->min_size * st->w);
   h = (st->min_size * st->h);

   evas_object_size_hint_min_set(st->ic, w, h);
   evas_object_move(st->bt, st->x, st->y);
   evas_object_show(st->ic);
   evas_object_show(st->bt);
   evas_object_smart_callback_add(st->ic, "clicked", _preview_clicked, info);
   st->status = EDJE_PICK_PRE_IMG_BLOW;
   evas_object_event_callback_add(st->ic, EVAS_CALLBACK_MOUSE_OUT,
       _preview_mouse_out_cb, info);
}

static Eina_Bool
_image_preview_timeout(void *data)
{  /* Here we handle appear/blow/shrink/close of preview window */
   gl_item_info *info = data;
   image_preview_st *st = info->preview;

   switch (st->status)
     {
      case EDJE_PICK_PRE_IMG_START:
           {
              if (!st->ic)
                {  /* Open window, change timer */
                   _show_image_preview(info);
                   st->tm = ecore_timer_add(EDJE_PICK_PREVIEW_ANIM,
                         _image_preview_timeout, info);

                   return ECORE_CALLBACK_CANCEL;
                }
              break;
           }

      case EDJE_PICK_PRE_IMG_BLOW:
           {
              if (st->ic)
                {
                   Evas_Coord x, y, w, h;
                   evas_object_geometry_get(st->ic, &x, &y, &w, &h);
                   if (w < ((st->min_size * st->w) * 2))
                     evas_object_size_hint_min_set(st->ic, w * 1.1, h * 1.1);
                   else
                     {
                        st->tm = NULL;
                        st->status = EDJE_PICK_PRE_IMG_STABLE;
                        return ECORE_CALLBACK_CANCEL;
                     }
                }
              break;
           }

      case EDJE_PICK_PRE_IMG_SHRINK:
           {
              if (st->ic)
                {
                   Evas_Coord x, y, w, h;
                   evas_object_geometry_get(st->ic, &x, &y, &w, &h);
                   if (w > (st->min_size * st->w))
                     evas_object_size_hint_min_set(st->ic, w * 0.9, h * 0.9);
                   else
                     {
                        st->status = EDJE_PICK_PRE_IMG_END;
                        evas_object_del(st->ic);
                        st->tm = NULL;
                        st->ic = NULL;
                        return ECORE_CALLBACK_CANCEL;
                     }
                }
              break;
           }

      case EDJE_PICK_PRE_IMG_STABLE:
      case EDJE_PICK_PRE_IMG_END:
      default:
           {
              st->tm = NULL;
              return ECORE_CALLBACK_CANCEL;
           }
     }

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_sample_preview_timeout(void *data)
{  /* Here we handle sample preview func */
   gl_item_info *info = data;
   sample_info_ex *ex = info->ex;
   sample_preview_st *st = info->preview;
   int r;
   int g;
   int b;
   int a;

   switch (st->status)
     {
      case EDJE_PICK_PRE_SMP_COLOR_INC:
           {
              evas_object_color_get(st->icon, &r, &g, &b, &a);

              if (g < 220)
                {
                   st->steps++;
                   evas_object_color_set(st->icon,
                         r + COLOR_STEP,
                         g + (COLOR_STEP + COLOR_STEP),
                         b + COLOR_STEP,
                         a + (COLOR_STEP + COLOR_STEP));
                }
              else
                {
                   char buf[1024];
                   st->status = EDJE_PICK_PRE_SMP_COLOR_DEC;
                   st->tm = NULL;

                   if (!st->sample)
                     {  /* Read sample info from file */
                        Eet_File *ef = eet_open(info->file_name,
                              EET_FILE_MODE_READ);
                        snprintf(buf, sizeof(buf), "edje/sounds/%i", ex->id);
                        st->sample = (void *) eet_read_direct(ef,
                              (const char *) buf, &st->size);
                        eet_close(ef);
                     }

                   if (st->sample)
                     {
                        snprintf(buf, sizeof(buf), "%i", ex->id);
                        edje_pick_sample_play(st->sample,
                              buf, st->size, 1.0, _play_sample_finished);
                     }
                   else
                     printf("<%s> Play sample - Failed to read.\n", __func__);

                   return ECORE_CALLBACK_RENEW;
                }
              break;
           }

      case EDJE_PICK_PRE_SMP_COLOR_DEC:
           {
              if (st->steps)
                {
                   st->steps--;
                   evas_object_color_get(st->icon, &r, &g, &b, &a);
                   evas_object_color_set(st->icon,
                         r - COLOR_STEP,
                         g - COLOR_STEP,
                         b - COLOR_STEP,
                         a - COLOR_STEP);

                   return ECORE_CALLBACK_RENEW;
                }
              else
                {
                   evas_object_color_set(st->icon, 255, 255, 255, 255);
                   st->status = EDJE_PICK_PRE_SMP_START;
                   st->tm = NULL;
                   return ECORE_CALLBACK_CANCEL;
                }

              break;
           }

      default:
           {
              st->tm = NULL;
              return ECORE_CALLBACK_CANCEL;
           }
     }

   return ECORE_CALLBACK_RENEW;
}

static void
_icon_mouse_in_cb(void *data,
               Evas *e __UNUSED__,
               Evas_Object *obj __UNUSED__,
               void *event_info __UNUSED__)
{
   gl_item_info *info = data;
   switch (info->type)
     {
      case EDJE_PICK_TYPE_IMAGE:
           {
              image_preview_st *st = info->preview;
              if (st->status != EDJE_PICK_PRE_IMG_DETACHED)
                {
                   if (st && (!st->ic) && (!st->tm))
                     {
                        st->status = EDJE_PICK_PRE_IMG_START;
                        st->tm = ecore_timer_add(EDJE_PICK_PREVIEW_TIMEOUT,
                              _image_preview_timeout, info);
                     }
                }
              break;
           }

      case EDJE_PICK_TYPE_SAMPLE:
           {
              sample_preview_st *st = info->preview;
              _sample_preview_stop(st);
              st->status = EDJE_PICK_PRE_SMP_COLOR_INC;
              evas_object_color_set(st->icon, INI_R, INI_G, INI_B, INI_A);
              st->tm = ecore_timer_add(EDJE_PICK_PREVIEW_ANIM,
                    _sample_preview_timeout, info);
              break;
           }

      default:
         break;
     }
}

static void
_icon_mouse_out_cb(void *data,
               Evas *e __UNUSED__,
               Evas_Object *obj __UNUSED__,
               void *event_info __UNUSED__)
{
   gl_item_info *info = data;
   switch (info->type)
     {
      case EDJE_PICK_TYPE_IMAGE:
           {
              image_preview_st *st = info->preview;
              if (st->status != EDJE_PICK_PRE_IMG_DETACHED)
                {
                   if ((!st->ic) && (st->tm))
                     {
                        ecore_timer_del(st->tm);
                        st->tm = NULL;
                     }
                }
              break;
           }

      case EDJE_PICK_TYPE_SAMPLE:
           {
              sample_preview_st *st = info->preview;
              if (st->status == EDJE_PICK_PRE_SMP_COLOR_INC)
                {
                   st->status = EDJE_PICK_PRE_SMP_COLOR_DEC;
                }
              break;
           }

      default:
         break;
     }
}

static Evas_Object *
_item_icon_create(gl_item_info *info, Evas_Object *parent,
      unsigned int *w, unsigned int *h)
{
   Evas_Object *icon = NULL;
   char buf[1024];
   switch (info->type)
     {
      case EDJE_PICK_TYPE_IMAGE:
           {
              icon = elm_icon_add(parent);
              Evas_Object *icon_img = elm_image_object_get(icon);
              if (_edje_pick_image_object_data_read(icon_img, info->ex,
                       info->file_name, w, h))
                {
                   evas_object_size_hint_aspect_set(icon,
                         EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
                   evas_object_show(icon);
                }

              return icon;
           }

      case EDJE_PICK_TYPE_FILE:
           {
              snprintf(buf, sizeof(buf), "%s/icons/file.png",
                    PACKAGE_DATA_DIR);
              icon = elm_icon_add(parent);
              break;
           }

      case EDJE_PICK_TYPE_GROUP:
           {
              snprintf(buf, sizeof(buf), "%s/icons/group.png",
                    PACKAGE_DATA_DIR);
              icon = elm_icon_add(parent);
              break;
           }

      case EDJE_PICK_TYPE_SAMPLE:
           {
              snprintf(buf, sizeof(buf), "%s/icons/sample.png",
                    PACKAGE_DATA_DIR);
              icon = elm_icon_add(parent);
              break;
           }

      default:
         break;
     }

   if (icon)
     {
        elm_image_file_set(icon, buf, NULL);
        evas_object_size_hint_aspect_set(icon,
              EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
        evas_object_show(icon);
     }

   return icon;
}

static Evas_Object *
_group_item_icon_get(void *data EINA_UNUSED, Evas_Object *parent EINA_UNUSED,
      const char *part EINA_UNUSED)
{  /* TODO: Set icon according to type field */
   Evas_Object *icon = NULL;
   if (!strcmp(part, "elm.swallow.icon"))
     {
        unsigned int w, h;
        gl_item_info *info = data;
        if (info->r)
          info = eina_list_data_get(eina_list_last(info->r));

        icon = _item_icon_create(info, parent, &w, &h);

   if (icon)
     {
        evas_object_event_callback_add (icon, EVAS_CALLBACK_MOUSE_IN,
              _icon_mouse_in_cb, info);
        evas_object_event_callback_add (icon, EVAS_CALLBACK_MOUSE_OUT,
              _icon_mouse_out_cb, info);
     }

        switch (info->type)
          {
           case EDJE_PICK_TYPE_IMAGE:
                {
                   image_preview_st *t = info->preview;
                   if (icon)
                     {
                        evas_object_size_hint_aspect_set(icon,
                              EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
                        evas_object_show(icon);

                        if (t)
                          {
                             t->icon = icon;
                             _image_preview_close(t);
                             t->status = EDJE_PICK_PRE_IMG_START;
                          }
                        else
                          {  /* Allocate preview struct on icon-success */
                             t = info->preview = calloc(1,
                                   sizeof(image_preview_st));

                             /* Record main-window pointer */
                             t->icon = icon;
                             t->status = EDJE_PICK_PRE_IMG_START;
                             t->w = w;
                             t->h = h;
                          }
                     }
                   break;
                }

           case EDJE_PICK_TYPE_SAMPLE:
                {
                   sample_preview_st *s = info->preview;
                   if (s)
                     {
                        s->icon = icon;
                        _sample_preview_stop(s);
                        s->status = EDJE_PICK_PRE_SMP_START;
                     }
                   else
                     {  /* Allocate preview struct on icon-success */
                        s = info->preview = calloc(1,
                              sizeof(sample_preview_st));

                        /* Record main-window pointer */
                        s->icon = icon;
                        s->status = EDJE_PICK_PRE_SMP_START;
                     }
                   break;
                }

           default:
              break;
          }
     }

   return icon;
}

static void
_font_preview_set(Elm_Object_Item *glit, gl_item_info *treeit)
{  /* Set tooltip for font preview */
   char ttip[256];  /* TODO: Just a stab-func, needs to be fixed later */
   sprintf(ttip, "<font=%s font_size=14 %s", treeit->name,  treeit->name);
   elm_object_tooltip_text_set(elm_object_item_widget_get(glit), ttip);
}

static void
gl_exp(void *data, Evas_Object *obj, void *event_info)
{
   gui_elements *g = data;
   Elm_Object_Item *glit = event_info;
   gl_item_info *head = elm_object_item_data_get(glit);
   gl_item_info *treeit;
   Eina_List *itr;

   Elm_Genlist_Item_Class *itc;
   Elm_Genlist_Item_Type iflag;

   EINA_LIST_FOREACH(head->sub, itr, treeit)
     {
        switch (treeit->type)
          {
           case EDJE_PICK_TYPE_FILE:
              iflag = ELM_GENLIST_ITEM_GROUP;
              itc = &g->itc_group;
              break;

           case EDJE_PICK_TYPE_LIST:
              iflag = ELM_GENLIST_ITEM_TREE;
              itc = &g->itc;
              break;

           default:
              iflag = ELM_GENLIST_ITEM_NONE;
              itc = &g->itc;
          }

        Elm_Object_Item *it =
           elm_genlist_item_append(obj, itc, treeit, glit, iflag, NULL, NULL);

        if (treeit->type == EDJE_PICK_TYPE_FONT)
          _font_preview_set(it, treeit);
     }
}

static void
gl_con(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   elm_genlist_item_subitems_clear(glit);
}

static void
gl_exp_req(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   elm_genlist_item_expanded_set(glit, EINA_TRUE);
}

static void
gl_con_req(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   elm_genlist_item_expanded_set(glit, EINA_FALSE);
}
/* END   - Genlist Item handling */

static void
_window_setting_update(gui_elements *g)
{
   if (g->file_name)
     {
        char *str = malloc(strlen(CLIENT_NAME) + strlen(g->file_name) + 32);
        sprintf(str, "%s%s- %s", g->file_name,
              ((g->modified) ? "* " : " "), CLIENT_NAME);

        elm_win_title_set(g->win, str);
        free(str);
     }
   else
     {
        char *str = malloc(strlen(CLIENT_NAME) +
              strlen(EDJE_PICK_NEW_FILE_NAME_STR) + strlen(" - %s") + 32);
        sprintf(str, "%s%s- %s", EDJE_PICK_NEW_FILE_NAME_STR,
              ((g->modified) ? "* " : " "), CLIENT_NAME);
        elm_win_title_set(g->win, str);
        free(str);
     }

   elm_object_item_disabled_set(g->menu_close,
         ((g->file_name == NULL) && (!g->modified)));
   elm_object_item_disabled_set(g->menu_save, (g->file_name == NULL));
   elm_object_item_disabled_set(g->menu_save_as,
         ((g->file_name == NULL) && (!g->modified)));
}

static void
_client_win_del(void *data,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{  /* called when client window is deleted */
   gui_elements *g = data;
   if (g->inwin)
     {  /* Close file selector if open */
        evas_object_del(g->inwin);
        g->inwin = NULL;
    }

   _gui_free(g);
   elm_exit(); /* exit the program's main loop that runs in elm_run() */
}

static Elm_Object_Item *
_glit_head_file_node_find(Evas_Object *gl, const char *file_name)
{
   if (file_name)
     {  /* May be NULL when calle for gl_dst */
        Elm_Object_Item *glit = elm_genlist_first_item_get(gl);
        while(glit)
          {
             gl_item_info *tmp;
             tmp = elm_object_item_data_get(glit);
             if ((tmp->type == EDJE_PICK_TYPE_FILE) &&
                   (!strcmp(tmp->name, file_name)))
               return glit;

             glit = elm_genlist_item_next_get(glit);
          }
     }

   return NULL;
}

static Elm_Object_Item *
_glit_head_list_node_find(Evas_Object *gl, const char *file, const char *name)
{
   Elm_Object_Item *glit = elm_genlist_first_item_get(gl);
   while(glit)
     {
        gl_item_info *tmp;
        tmp = elm_object_item_data_get(glit);
        Eina_Bool fn = (file && tmp->file_name) ?
           (!strcmp(file, tmp->file_name)) : EINA_TRUE;

        if (((tmp->type == EDJE_PICK_TYPE_LIST) &&
                 (!strcmp(tmp->name, name))) && fn)
          return glit;

        glit = elm_genlist_item_next_get(glit);
     }

   return NULL;
}

static Elm_Object_Item *
_glit_node_find(Evas_Object *gl, gl_item_info *info)
{  /* Locate the genlist item according to data pointer */
   Elm_Object_Item *glit = elm_genlist_first_item_get(gl);
   while(glit)
     {
        gl_item_info *tmp;
        tmp = elm_object_item_data_get(glit);
        if (tmp == info)
          return glit;

        glit = elm_genlist_item_next_get(glit);
     }

   return NULL;
}

static Elm_Object_Item *
_glit_node_find_by_info(Evas_Object *gl,
      char *file_name,
      Edje_Pick_Type type,
      char *name)
{  /* Locate the genlist item according to given info */
   Elm_Object_Item *glit;
   const char *type_str = NULL;
   switch (type)
     {
      case EDJE_PICK_TYPE_FILE:
           {
              glit = _glit_head_file_node_find(
                    gl, file_name);
              break;
           }

      case EDJE_PICK_TYPE_LIST:
           {
              glit = _glit_head_list_node_find(
                    gl, file_name, name);
              break;
           }
         break;

      case EDJE_PICK_TYPE_GROUP:
         type_str = EDJE_PICK_GROUPS_STR;
      case EDJE_PICK_TYPE_IMAGE:
         if (type_str == NULL) type_str = EDJE_PICK_IMAGES_STR;
      case EDJE_PICK_TYPE_SAMPLE:
         if (type_str == NULL) type_str = EDJE_PICK_SAMPLES_STR;
      case EDJE_PICK_TYPE_FONT:
           {  /* Find the group, image, smaple, or font item */
              if (type_str == NULL) type_str = EDJE_PICK_FONTS_STR;
              glit = _glit_head_list_node_find(
                    gl, file_name,
                    type_str);
              if (glit)
                {
                   gl_item_info *info =
                      elm_object_item_data_get(glit);

                   info = eina_list_search_unsorted(
                         info->sub,
                         _item_name_cmp, name);

                   /* Find the GL group node */
                   glit = _glit_node_find(gl, info);
                }
              break;
           }
         break;


      default:
         glit = NULL;
     }

   return glit;
}

static char *
_list_string_get(Edje_Pick_Type type)
{
   switch (type)
     {
      case EDJE_PICK_TYPE_GROUP:
         return EDJE_PICK_GROUPS_STR;

      case EDJE_PICK_TYPE_IMAGE:
         return EDJE_PICK_IMAGES_STR;

      case EDJE_PICK_TYPE_SAMPLE:
         return EDJE_PICK_SAMPLES_STR;

      case EDJE_PICK_TYPE_FONT:
         return EDJE_PICK_FONTS_STR;

      default:
         return NULL;
     }
}

static gl_item_info *
_gl_data_find(Evas_Object *gl, const char *file,
      Edje_Pick_Type type, const char *name)
{  /* Find if this data inlcuded in the genlist (even if not shown) */
   Elm_Object_Item *it = _glit_head_file_node_find(gl, file);
   gl_item_info *file_info = ((it) ? elm_object_item_data_get(it) : NULL);
   gl_item_info *list_info = NULL;
   char *list_str = NULL;

   switch (type)
     {
      case EDJE_PICK_TYPE_FILE:
         return file_info;

      case EDJE_PICK_TYPE_LIST:
           {
              if (file_info)
                return eina_list_search_unsorted(file_info->sub,
                      _item_name_cmp, name);
              else
                {  /* May look for list node in gl_dst */
                   it = _glit_head_list_node_find(gl, file, name);
                   list_info = ((it) ? elm_object_item_data_get(it) : NULL);
                   return list_info;
                }

              break;
           }

      default:
         list_str = _list_string_get(type);
     }

   if (list_str)
     {  /* Search for leaf data */
        if (file_info)
          list_info = eina_list_search_unsorted(file_info->sub,
                _item_name_cmp, list_str);
        else
          {  /* May look for list node in gl_dst */
             it = _glit_head_list_node_find(gl, file, list_str);
             list_info = ((it) ? elm_object_item_data_get(it) : NULL);
          }

        if (list_info)
          return eina_list_search_unsorted(list_info->sub,
                _item_name_cmp, name);
     }

   return NULL;
}

static void
_cancel_popup(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   gui_elements *g = data;
   evas_object_del(g->popup);
   g->popup = NULL;
}

static void
_ok_popup_show(gui_elements *g,
      Evas_Smart_Cb func, char *title, const char *err)
{
   g->popup = elm_popup_add(g->win);
   evas_object_size_hint_weight_set(g->popup,
         EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

   // popup text
   elm_object_text_set(g->popup, err);
   // popup title
   elm_object_part_text_set(g->popup, "title,text", title);

   // popup buttons
   Evas_Object *btn = elm_button_add(g->popup);
   elm_object_text_set(btn, "Close");
   elm_object_part_content_set(g->popup, "button1", btn);
   evas_object_smart_callback_add(btn, "clicked", func, g);

   evas_object_show(g->popup);
}

static gl_item_info *
_groups_get(Eina_List *grp, const char *file_name)
{
   gl_item_info *list_info = NULL;
   gl_item_info *child;
   Eina_List *l;
   const char *name;

   EINA_LIST_FOREACH(grp, l, name)
     {
        if (!list_info)
          {
             list_info = calloc(1, sizeof(gl_item_info));
             list_info->file_name = eina_stringshare_add(file_name);
             list_info->type = EDJE_PICK_TYPE_LIST;
             list_info->name = eina_stringshare_add(EDJE_PICK_GROUPS_STR);
          }

        child = calloc(1, sizeof(gl_item_info));
        child->file_name = eina_stringshare_add(file_name);
        child->type = EDJE_PICK_TYPE_GROUP;
        child->name = eina_stringshare_add(name);
        list_info->sub = eina_list_append(list_info->sub, child);
     }

   return list_info;
}

static gl_item_info *
_images_get(Eina_List *img, const char *file_name)
{
   gl_item_info *list_info = NULL;
   gl_item_info *child;
   Eina_List *l;
   image_info_ex *ex;

   EINA_LIST_FOREACH(img, l, ex)
     {
        if (!list_info)
          {
             list_info = calloc(1, sizeof(gl_item_info));
             list_info->file_name = eina_stringshare_add(file_name);
             list_info->type = EDJE_PICK_TYPE_LIST;
             list_info->name = eina_stringshare_add(EDJE_PICK_IMAGES_STR);
          }

        child = calloc(1, sizeof(gl_item_info));
        child->file_name = eina_stringshare_add(file_name);
        child->type = EDJE_PICK_TYPE_IMAGE;
        child->name = eina_stringshare_add(ex->name);
        child->ex = ex;
        list_info->sub = eina_list_append(list_info->sub, child);
     }

   return list_info;
}

static gl_item_info *
_samples_get(Eina_List *smp, const char *file_name)
{
   gl_item_info *list_info = NULL;
   gl_item_info *child;
   Eina_List *l;
   image_info_ex *ex;

   EINA_LIST_FOREACH(smp, l, ex)
     {
        if (!list_info)
          {
             list_info = calloc(1, sizeof(gl_item_info));
             list_info->file_name = eina_stringshare_add(file_name);
             list_info->type = EDJE_PICK_TYPE_LIST;
             list_info->name = eina_stringshare_add(EDJE_PICK_SAMPLES_STR);
          }

        child = calloc(1, sizeof(gl_item_info));
        child->file_name = eina_stringshare_add(file_name);
        child->type = EDJE_PICK_TYPE_SAMPLE;
        child->name = eina_stringshare_add(ex->name);
        child->ex = ex;
        list_info->sub = eina_list_append(list_info->sub, child);
     }

   return list_info;
}

static gl_item_info *
_fonts_get(Eina_List *fnt, const char *file_name)
{
   gl_item_info *list_info = NULL;
   gl_item_info *child;
   Eina_List *l;
   font_info_ex *ex;

   EINA_LIST_FOREACH(fnt, l, ex)
     {
        if (!list_info)
          {
             list_info = calloc(1, sizeof(gl_item_info));
             list_info->file_name = eina_stringshare_add(file_name);
             list_info->type = EDJE_PICK_TYPE_LIST;
             list_info->name = eina_stringshare_add(EDJE_PICK_FONTS_STR);
          }

        child = calloc(1, sizeof(gl_item_info));
        child->file_name = eina_stringshare_add(file_name);
        child->type = EDJE_PICK_TYPE_FONT;
        child->name = eina_stringshare_add(ex->name);
        child->ex = ex;
        list_info->sub = eina_list_append(list_info->sub, child);
     }

   return list_info;
}

static Elm_Object_Item *
_load_file(Evas_Object *gl,
      Elm_Genlist_Item_Class *itc_group,
      Elm_Genlist_Item_Class *itc,
      void *data, Evas_Object *obj EINA_UNUSED,
      const char *file_name)
{
   gui_elements *g = data;

   if (file_name)
     {  /* Got file name, read goupe names and add to genlist */
        Elm_Object_Item *file_glit = NULL;

        gl_item_info *file_info = NULL;
        gl_item_info *groups_info = NULL;
        gl_item_info *images_info = NULL;
        gl_item_info *samples_info = NULL;
        gl_item_info *fonts_info = NULL;

        const char *err = NULL;
        Eina_List *grp = NULL;  /* Groups  info list */
        Eina_List *img = NULL;  /* Images  info list */
        Eina_List *smp = NULL;  /* Sampels info list */
        Eina_List *fnt = NULL;  /* Fonts info list   */

        int s = edje_pick_file_info_read(file_name, &grp, &img, &smp, &fnt);
        if (s != EDJE_PICK_NO_ERROR)
          {
             eina_list_free(grp);
             err = edje_pick_err_str_get(s);
             _ok_popup_show(data, _cancel_popup, "File Error", err);
             return file_glit;
          }

        if (gl == g->gl_src)
          {  /* Allocate file-info only if this is source genlist */
             file_info = calloc(1, sizeof(gl_item_info));
             file_info->file_name = eina_stringshare_add(file_name);
             file_info->type = EDJE_PICK_TYPE_FILE;
             file_info->name = eina_stringshare_add(file_name);
          }

        groups_info = _groups_get(grp, file_name);
        images_info = _images_get(img, file_name);
        samples_info = _samples_get(smp, file_name);
        fonts_info = _fonts_get(fnt, file_name);

        if (file_info)
          {  /* only if gl is gl_src */
             if (groups_info)
               file_info->sub = eina_list_append(file_info->sub, groups_info);

             if (images_info)
               file_info->sub = eina_list_append(file_info->sub, images_info);

             if (samples_info)
               file_info->sub = eina_list_append(file_info->sub, samples_info);

             if (fonts_info)
               file_info->sub = eina_list_append(file_info->sub, fonts_info);

             file_glit = elm_genlist_item_append(gl, itc_group,
                   file_info, NULL,
                   ELM_GENLIST_ITEM_GROUP, NULL, NULL);
          }

        if (groups_info)
          {
             elm_genlist_item_append(gl, itc,
                   groups_info, file_glit,
                   ELM_GENLIST_ITEM_TREE, NULL, NULL);
          }

        if (images_info)
          {
             elm_genlist_item_append(gl, itc,
                   images_info, file_glit,
                   ELM_GENLIST_ITEM_TREE, NULL, NULL);
          }

        if (samples_info)
          {
             elm_genlist_item_append(gl, itc,
                   samples_info, file_glit,
                   ELM_GENLIST_ITEM_TREE, NULL, NULL);
          }

        if (fonts_info)
          {
             elm_genlist_item_append(gl, itc,
                   fonts_info, file_glit,
                   ELM_GENLIST_ITEM_TREE, NULL, NULL);
          }


        eina_list_free(grp);
        eina_list_free(img);
        eina_list_free(smp);
        eina_list_free(fnt);

        if (gl == g->gl_dst)
          {
             if (g->file_name)
               eina_stringshare_del(g->file_name);

             g->file_name = eina_stringshare_add(file_name);
             g->modified = EINA_FALSE;
             _window_setting_update(g);
          }

        /* Clear UNDO / REDO  list each time we load a file */
        _actions_list_clear(&(g->actions));
        return file_glit;
     }

   return NULL;
}

static Elm_Object_Item *
_load_ok(void *data, Evas_Object *obj, void *event_info)
{
   gui_elements *g = data;
   return _load_file(event_info, &(g->itc_group), &(g->itc),
         data, obj, g->file_to_open);
}

static void
_include_bt_do(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   gui_elements *g = data;
   if (g->inwin)
     {  /* Called for done, selected, free only once */
        evas_object_del(g->inwin);
        g->inwin = NULL;
     }

   if (event_info)
     {
        if (g->file_to_open)
          eina_stringshare_del(g->file_to_open);

        g->file_to_open = eina_stringshare_add(event_info);

        Elm_Object_Item *file_glit =
           _glit_head_file_node_find(g->gl_src, g->file_to_open);

        if (file_glit)
          {
             char *err = malloc(strlen(g->file_to_open) +
                   strlen("File: '") +
                   strlen("' already included"));

             sprintf(err, "File: '%s' already included", g->file_to_open);
             _ok_popup_show(data, _cancel_popup, "File Included", err);
             free(err);
          }
        else
          _load_ok(data, obj, g->gl_src);
     }
}

static void
_file_selector_show(gui_elements *g,  Evas_Smart_Cb func, Eina_Bool is_save)
{
   if (g->inwin)
     {  /* Called for done, selected, free only once */
        evas_object_del(g->inwin);
        g->inwin = NULL;
     }

   g->inwin = elm_win_inwin_add(g->win);
   Evas_Object *fs = elm_fileselector_add(g->win);
   /* enable the fs file name entry */
   elm_fileselector_is_save_set(fs, is_save);
   /* make the file list a tree with dir expandable in place */
   elm_fileselector_expandable_set(fs, EINA_FALSE);

   evas_object_smart_callback_add(fs, "done", func, g);
   /* start the fileselector in the home dir */
   elm_fileselector_path_set(fs, getenv("HOME"));

   elm_win_inwin_content_set(g->inwin, fs);
   evas_object_show(g->inwin);
}

static char **
_command_line_args_make(gui_elements *g, Eina_List *s,
      const char *prog, const char *outfile, int *argc,
      Eina_Bool strict)
{  /* User has to free returned char ** */
#define ADD_ARG(LIST, STR, CNT) do{ \
     LIST = eina_list_append(LIST, STR); \
     CNT++; \
}while(0);
   char **argv = NULL;
   Eina_List *args = NULL;
   Eina_List *l;
   Elm_Object_Item *it;
   gl_item_info *info;
   unsigned int i;

   ADD_ARG(args, prog, (*argc));  /* Add program-name as argv[0] */

   it = _glit_head_list_node_find(g->gl_dst, NULL, EDJE_PICK_GROUPS_STR);
   if (it)
     {  /* Run through taken-groups and build args-list */
        info = elm_object_item_data_get(it);
        gl_item_info *group;
        EINA_LIST_FOREACH(info->sub, l, group)
          {
             ADD_ARG(args, "-i", (*argc));
             ADD_ARG(args, group->file_name, (*argc));
             ADD_ARG(args, "-g", (*argc));
             ADD_ARG(args, group->name, (*argc));
          }
     }

   EINA_LIST_FOREACH(s, l, info)
     {  /* Run through selected-items and build args-list */
        switch(info->type)
          {
           case EDJE_PICK_TYPE_FILE:
                {  /* Update pointer to Groups pointer, and continue in LIST */
                   if (strict)
                     {
                        info = eina_list_search_unsorted(info->sub,
                              _item_name_cmp, EDJE_PICK_GROUPS_STR);
                     }
                }

           case EDJE_PICK_TYPE_LIST:
                {
                   if (strict)
                     {  /* Makes sure no duplicate groups */
                        gl_item_info *group;
                        Eina_List *ll;
                        ADD_ARG(args, "-i", (*argc));
                        ADD_ARG(args, info->file_name, (*argc));
                        EINA_LIST_FOREACH(info->sub, ll, group)
                          {
                             ADD_ARG(args, "-g", (*argc));
                             ADD_ARG(args, group->name, (*argc));
                          }
                     }
                   else
                     {  /* Include all groups from a file */
                        ADD_ARG(args, "-a", (*argc));
                        ADD_ARG(args, info->file_name, (*argc));
                     }
                   break;
                }

           case EDJE_PICK_TYPE_GROUP:
                {  /* Include a specific group from a file */
                   ADD_ARG(args, "-i", (*argc));
                   ADD_ARG(args, info->file_name, (*argc));
                   ADD_ARG(args, "-g", (*argc));
                   ADD_ARG(args, info->name, (*argc));
                   break;
                }

           default:
              break;
          }
     }

   ADD_ARG(args, "-o", (*argc));
   ADD_ARG(args, outfile, (*argc));  /* Add output file name arg */

   /* Compose argv-like string array */
   argv = calloc(eina_list_count(args), sizeof(char *));
   for(i = 0; i < eina_list_count(args); i++)
     {
        argv[i] = eina_list_nth(args, i);
        printf("%s ", argv[i]);
     }

   eina_list_free(args);
   return argv;
#undef ADD_ARG
}


static Elm_Object_Item *
_file_item_add(gui_elements *g, Evas_Object *gl, const char *file)
{
   Elm_Object_Item *it;
   /* Add file-name as tree-head for groups */
   gl_item_info *head = calloc(1, sizeof(gl_item_info));
   head->file_name = eina_stringshare_add(file);
   head->type = EDJE_PICK_TYPE_FILE;
   head->name = eina_stringshare_add(file);

   it = elm_genlist_item_append(gl, &(g->itc_group),
         head, NULL,
         ELM_GENLIST_ITEM_GROUP, NULL, NULL);

   return it;
}

static Elm_Object_Item *
_list_item_add(gui_elements *g, Evas_Object *gl,
      const char *file, const char *n, Eina_Bool f)
{
   Elm_Object_Item *it;
   Elm_Object_Item *file_glit = NULL;
   gl_item_info *file_info;
   gl_item_info *group_head = calloc(1, sizeof(gl_item_info));
   group_head->file_name = (file) ? eina_stringshare_add(file) : NULL;
   group_head->type = EDJE_PICK_TYPE_LIST;
   group_head->name = eina_stringshare_add(n);

   if (f)
     {  /* Need to find/add file genlist item */
        file_glit = _glit_head_file_node_find(gl, file);
        if (!file_glit)
          file_glit = _file_item_add(g, gl, file);

        file_info = elm_object_item_data_get(file_glit);
        file_info->sub = eina_list_append(file_info->sub, group_head);
     }

   it = elm_genlist_item_append(gl, &(g->itc),
         group_head, file_glit,
         EINA_TRUE, NULL, NULL);

   return it;
}

static void
_leaf_item_update(gui_elements *g,
      Evas_Object *src, Evas_Object *dst,
      gl_item_info *info, Eina_List **plm)
{  /* Add a replacment-struct to info */
   /* Locate same-type selected info in dst list */
   Eina_List *l;
   Elm_Object_Item *it;
   Eina_Bool redo = (!plm) && (dst == g->gl_dst);

   /* Use src as dst in case of an undo */
   const Eina_List *slct = elm_genlist_selected_items_get((plm || redo) ?
         dst : src);

   EINA_LIST_FOREACH((Eina_List *) slct, l, it)
     {  /* Build a list of all selected-items infos */
        gl_item_info *t = elm_object_item_data_get(it);
        if (t->type == info->type)
          {  /* Find item to update in dest list */
             if (plm || redo)
               {  /* Commit update, do not reg action on redo */
                  t->r = eina_list_append(t->r, info);
                  elm_genlist_item_update(it);

                  if (plm) /* Register this action in UNDO, REDO list */
                    *plm = eina_list_append(*plm, info);
               }
             else
               {  /* We are doing an undo */
                  t->r = eina_list_remove_list(t->r, eina_list_last(t->r));
                  elm_genlist_item_update(it);
               }

             return;
          }
     }
}

static void
_leaf_item_move(gui_elements *g,
      Evas_Object *src, Evas_Object *dst,
      gl_item_info *info, Eina_List **plm)
{
   Elm_Object_Item *it = _glit_node_find(src, info);
   Elm_Object_Item *pit = NULL;
   const char *file = (dst == g->gl_src) ? (info->file_name) : NULL;

   switch (info->type)
     {
      case EDJE_PICK_TYPE_IMAGE:
      case EDJE_PICK_TYPE_SAMPLE:
      case EDJE_PICK_TYPE_FONT:
           {  /* Do an update procedure for these types instead */
              return _leaf_item_update(g, src, dst, info, plm);
           }

      default:
         break;
     }

   if (!_gl_data_find(dst, file, info->type, info->name))
     {  /* Add leaf-data to dest genlist */
        gl_item_info *list_info = NULL;
        char *list_str = _list_string_get(info->type);
        Elm_Object_Item *ithd = _glit_head_list_node_find(dst, file, list_str);
        if (!ithd) /* Make list node and parent if needed */
          ithd = _list_item_add(g, dst, file, list_str, dst == g->gl_src);

        list_info = elm_object_item_data_get(ithd);
        list_info->sub = eina_list_append(list_info->sub, info);
        if (elm_genlist_item_expanded_get(ithd))
          {
             Elm_Object_Item *itlf =
                elm_genlist_item_append(dst, &(g->itc), info, ithd,
                      ELM_GENLIST_ITEM_NONE, NULL, NULL);

             elm_genlist_item_update(ithd);

             if (info->type == EDJE_PICK_TYPE_FONT)
               _font_preview_set(itlf, info);
          }
     }


   if (it)
     {  /* Will find it only if displayed, not show do 'else' clause */
        pit = elm_genlist_item_parent_get(it);
        elm_object_item_del(it);
     }
   else
     {  /* We have to locate parent, even when leaf contracted (not shown) */
        if (_gl_data_find(src, info->file_name, info->type, info->name))
          {  /* find apropriate list-data */
             char *list_str = _list_string_get(info->type);
             pit = _glit_head_list_node_find(src, info->file_name, list_str);
          }
     }

   if (pit)
     {
        gl_item_info *pinfo = elm_object_item_data_get(pit);
        pinfo->sub = eina_list_remove(pinfo->sub, info);
        if (elm_genlist_item_expanded_get(pit))
          elm_genlist_item_update(pit);
     }

 if (plm)  /* Register this action in UNDO, REDO list */
   *plm = eina_list_append(*plm, info);
}

static Eina_List *
_list_item_move(gui_elements *g, Eina_List *del,
      Evas_Object *src, Evas_Object *dst,
      gl_item_info *info, Eina_List **plm)
{  /* Remove list-node from source in found and copy all sub-leafs */
   Eina_List *l, *l_next;
   const char *fn = (dst == g->gl_dst) ? (info->file_name) : NULL;
   Elm_Object_Item *pit = NULL;
   gl_item_info *tmp;
   Elm_Object_Item *it = _glit_head_list_node_find(src, fn, info->name);
   if (it)
     {
        pit = elm_genlist_item_parent_get(it);
        elm_object_item_del(it);
     }

   EINA_LIST_FOREACH_SAFE(info->sub, l, l_next, tmp)
      _leaf_item_move(g, src, dst, tmp, plm);

   eina_list_free(info->sub);
   info->sub = NULL;

   _gl_item_data_free(info);
   del = eina_list_append(del, info);

   if (pit)
     {
        gl_item_info *pinfo = elm_object_item_data_get(pit);
        pinfo->sub = eina_list_remove(pinfo->sub, info);
     }

   return del;
}

static Eina_List *
_file_item_move(gui_elements *g, Eina_List *del,
      Evas_Object *src, Evas_Object *dst,
      gl_item_info *info, Eina_List **plm)
{  /* Remove file-node from source in found and copy all sub-lists */
   Eina_List *l, *l_next;
   gl_item_info *tmp;
   Elm_Object_Item *it = _glit_head_file_node_find(src, info->name);
   if (it)
     elm_object_item_del(it);

   EINA_LIST_FOREACH_SAFE(info->sub, l, l_next, tmp)
      del = _list_item_move(g, del, src, dst, tmp, plm);

   eina_list_free(info->sub);
   info->sub = NULL;

   _gl_item_data_free(info);
   del = eina_list_append(del, info);
   return del;
}

static Eina_List *
_edje_pick_remove_from_parent(Elm_Object_Item *it, Eina_List *del)
{  /* Remove item from parent and remove parent with no leafs */
   Elm_Object_Item *pit = elm_genlist_item_parent_get(it);
   gl_item_info *info = elm_object_item_data_get(it);
   elm_object_item_del(it);

   if (pit)
     {
        gl_item_info *pinfo = elm_object_item_data_get(pit);
        pinfo->sub = eina_list_remove(pinfo->sub, info);
        if (!pinfo->sub)
          del = _edje_pick_remove_from_parent(pit, del);
     }

   if (!eina_list_search_unsorted(del, _item_ptr_cmp, info))
     {
        _gl_item_data_free(info);
        del = eina_list_append(del, info);
     }

   return del;
}

static void
_edje_pick_items_move(gui_elements *g,
      Evas_Object *src, Evas_Object *dst,
      Eina_List *s, Eina_Bool reg)
{  /* Move all items in s from src to dest */
   Eina_List *items = eina_list_clone(s);
   Eina_List *l;
   Elm_Object_Item *it;
   gl_item_info *info;
   Eina_List *deleted_infos = NULL;
   Eina_List *leafs_moved = NULL;  /* Will be used for UNDO / REDO */
   Eina_List **plm = (reg) ? (&leafs_moved) : NULL;

   /* Sort items so files are first */
   items = eina_list_sort(items, eina_list_count(items), _item_type_cmp);

   EINA_LIST_FOREACH(items, l, info)
     {  /* Create items in dest-list */
        if (eina_list_search_unsorted(deleted_infos, _item_ptr_cmp, info))
          continue;  /* Skip deleted (from parent removal) ones */

        switch (info->type)
          {
           case EDJE_PICK_TYPE_FILE:
                {
                   deleted_infos = _file_item_move(g, deleted_infos,
                         src, dst, info, plm);
                   break;
                }

           case EDJE_PICK_TYPE_LIST:
                {
                   deleted_infos = _list_item_move(g, deleted_infos,
                         src, dst, info, plm);
                   break;
                }

           case EDJE_PICK_TYPE_GROUP:
           case EDJE_PICK_TYPE_IMAGE:
           case EDJE_PICK_TYPE_SAMPLE:
           case EDJE_PICK_TYPE_FONT:
                {
                   _leaf_item_move(g, src, dst, info, plm);
                   break;
                }

           default:
              break;
          }
     }


   {  /* Remove parents with no sub-items */
      it = elm_genlist_first_item_get(src);
      while(it)
        {
           info = elm_object_item_data_get(it);
           if (((info->type == EDJE_PICK_TYPE_FILE) ||
                    (info->type == EDJE_PICK_TYPE_LIST)) && (!info->sub))
             {
                deleted_infos =
                   _edje_pick_remove_from_parent(it, deleted_infos);
                it = elm_genlist_first_item_get(src);
             }
           else
             it = elm_genlist_item_next_get(it);
        }
   }

 eina_list_free(deleted_infos);
 eina_list_free(items);

 if (reg)
   _actions_list_add(&(g->actions), leafs_moved, src, dst);

 g->modified = EINA_TRUE;
 _window_setting_update(g);
}

static void
_take_bt_clicked(void *data EINA_UNUSED,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{  /* Copy selected items */
   gui_elements *g = data;
   Elm_Object_Item *it;
   Eina_List *l;
   Eina_List *s = NULL;
   const Eina_List *slct = elm_genlist_selected_items_get(g->gl_src);

   EINA_LIST_FOREACH((Eina_List *) slct, l, it)
     {  /* Build a list of all selected-items infos */
        s = eina_list_append(s, elm_object_item_data_get(it));
     }

   /* First check OK to move groups */
   if (s)
     {
        int argc = 0;
        Edje_Pick_Status status = EDJE_PICK_NO_ERROR;
        char **argv = _command_line_args_make(g, s, g->argv0,
              "/tmp/out.tmp", &argc, EINA_TRUE);

        edje_pick_context_set(g->context);
        status = edje_pick_command_line_parse(argc, argv,
              NULL, NULL, EINA_TRUE);

        free(argv);
        if (status != EDJE_PICK_NO_ERROR)
          {  /* Parse came back with an error */
             printf("%s\n", edje_pick_err_str_get(status));
             _ok_popup_show(data, _cancel_popup, "Error",
                   (char *) edje_pick_err_str_get(status));
          }
        else
          {  /* Add the selection to dest list */
             printf("\n\nParse OK\n");
             _edje_pick_items_move(g, g->gl_src, g->gl_dst, s, EINA_TRUE);
          }
        eina_list_free(s);
     }

   return;
}

static void
_remove_bt_clicked(void *data EINA_UNUSED,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{  /* Remove selected files, groups */
   gui_elements *g = data;
   Elm_Object_Item *it;
   Eina_List *l;
   Eina_List *s = NULL;
   const Eina_List *slct = elm_genlist_selected_items_get(g->gl_dst);

   EINA_LIST_FOREACH((Eina_List *) slct, l, it)
     {  /* Build a list of all selected-items infos */
        s = eina_list_append(s, elm_object_item_data_get(it));
     }

   if (s)
     {
        _edje_pick_items_move(g, g->gl_dst, g->gl_src, s, EINA_TRUE);
        eina_list_free(s);
     }

   return;
}

static void
_undo_bt_clicked(void *data EINA_UNUSED,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   gui_elements *g = data;
   g->actions.c--;
   action_st *st = eina_list_nth(g->actions.act, g->actions.c);

   elm_object_item_disabled_set(g->actions.redo_bt,
         g->actions.c == eina_list_count(g->actions.act));

   elm_object_item_disabled_set(g->actions.undo_bt, (g->actions.c == 0));

     {  /* Commit the actual undo */
        Eina_List *t = eina_list_clone(st->list);
        _edje_pick_items_move(g, st->gl_dst, st->gl_src, t, EINA_FALSE);
        eina_list_free(t);
     }
}

static void
_redo_bt_clicked(void *data EINA_UNUSED,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   gui_elements *g = data;
   action_st *st = eina_list_nth(g->actions.act, g->actions.c);
   g->actions.c++;

   elm_object_item_disabled_set(g->actions.redo_bt,
         g->actions.c == eina_list_count(g->actions.act));

   elm_object_item_disabled_set(g->actions.undo_bt, (g->actions.c == 0));

     {  /* Commit the actual redo */
        Eina_List *t = eina_list_clone(st->list);
        _edje_pick_items_move(g, st->gl_src, st->gl_dst, t, EINA_FALSE);
        eina_list_free(t);
     }
}

static void
_ok_cancel_popup_show(gui_elements *g, const char *title, const char *msg,
      const char *ok, const char *cancel,
      Evas_Smart_Cb ok_func, Evas_Smart_Cb cancel_func)
{
   Evas_Object *btn, *btn2;

   g->popup = elm_popup_add(g->win);
   evas_object_size_hint_weight_set(g->popup, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

   // popup text
   elm_object_text_set(g->popup, msg);
   // popup title
   elm_object_part_text_set(g->popup, "title,text", title);

   // popup buttons
   btn = elm_button_add(g->popup);
   elm_object_text_set(btn, ok);
   elm_object_part_content_set(g->popup, "button1", btn);
   evas_object_smart_callback_add(btn, "clicked", ok_func, g);

   btn2 = elm_button_add(g->popup);
   elm_object_text_set(btn2, cancel);
   elm_object_part_content_set(g->popup, "button2", btn2);
   evas_object_smart_callback_add(btn2, "clicked", cancel_func, g);

   evas_object_show(g->popup);
}

static void
_do_open(void *data, Evas_Object *obj __UNUSED__,
             void *event_info __UNUSED__)
{
   gui_elements *g = data;
   if (g->popup)
     {
        evas_object_del(g->popup);
        g->popup = NULL;
     }

   eina_stringshare_del(g->file_name);
   g->file_name = NULL;

   _gl_data_free(g->gl_dst);
   elm_genlist_clear(g->gl_dst);
   _load_file(g->gl_dst, &(g->itc_group), &(g->itc),
         data, g->gl_dst, g->file_to_open);
}

static void
_open_file(void *data, Evas_Object *obj __UNUSED__, void *event_info)
{
#define RELOAD_MSG "File '%s' is open, reload?"

   gui_elements *g = data;
   if (g->inwin)
     {  /* Called for done, selected, free only once */
        evas_object_del(g->inwin);
        g->inwin = NULL;
     }

   if (event_info)
     {
        if (g->file_to_open)
          eina_stringshare_del(g->file_to_open);

        g->file_to_open = eina_stringshare_add(event_info);

        if (g->file_name && (!strcmp(g->file_name, event_info)))
          {  /* This file is already open */
             char *buf = malloc(strlen(RELOAD_MSG) + strlen(g->file_name) + 1);
             sprintf(buf, RELOAD_MSG, g->file_name);

             _ok_cancel_popup_show(g, "File is open", buf,
                   "RELOAD", "CANCEL",
                   _do_open, _cancel_popup);

             free(buf);
          }
        else
          _do_open(data, NULL, NULL);
     }
#undef RELOAD_MSG
}

static void
_discard_btn_clicked(void *data, Evas_Object *obj __UNUSED__,
             void *event_info __UNUSED__)
{
   gui_elements *g = data;
   evas_object_del(g->popup);
   g->popup = NULL;

   _file_selector_show(g, _open_file, EINA_FALSE);
}

static void
_open_bt_clicked(void *data,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
#define DISCARD_MSG "File '%s' was modified, Discard changes?"
   gui_elements *g = data;

   if (g->modified)
     {
        const char *name = (g->file_name) ?
           g->file_name : EDJE_PICK_NEW_FILE_NAME_STR;

        char *buf = malloc(strlen(DISCARD_MSG) + strlen(name) + 1);
        sprintf(buf, DISCARD_MSG, name);

        _ok_cancel_popup_show(g, "Discard Changes", buf, "YES", "NO",
           _discard_btn_clicked , _cancel_popup);

        free(buf);
     }
   else
     {
        _file_selector_show(g, _open_file, EINA_FALSE);
     }
#undef DISCARD_MSG
}

static void
_do_close(void *data, Evas_Object *obj __UNUSED__,
             void *event_info __UNUSED__)
{
   gui_elements *g = data;
   if (g->popup)
     {
        evas_object_del(g->popup);
        g->popup = NULL;
     }

   if (g->file_name)
     {
        eina_stringshare_del(g->file_name);
        g->file_name = NULL;
     }

   _gl_data_free(g->gl_dst);
   elm_genlist_clear(g->gl_dst);
   g->modified = EINA_FALSE;
   _window_setting_update(g);
}

static void
_close_bt_clicked(void *data,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
#define CLOSE_MSG "File '%s' was modified, close without saving?"
   gui_elements *g = data;

   if (g->modified)
     {
        const char *name = (g->file_name) ?
           g->file_name : EDJE_PICK_NEW_FILE_NAME_STR;

        char *buf = malloc(strlen(CLOSE_MSG) + strlen(name) + 1);
        sprintf(buf, CLOSE_MSG, name);

        _ok_cancel_popup_show(g, "Close file", buf, "YES", "NO",
           _do_close , _cancel_popup);

        free(buf);
     }
   else
     {
        _do_close(g, NULL, NULL);
     }
#undef CLOSE_MSG
}

static void
_save_bt_clicked(void *data,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{

   gui_elements *g = data;
   char **argv = NULL;
   int i, argc = 0;

   /* Compose a temporary output file name */
   char *tmp_file_name = malloc(strlen(g->file_name) + 16);
   sprintf(tmp_file_name, "%s_%lld", g->file_name, (long long) time(NULL));
   argv = _command_line_args_make(g, NULL, g->argv0, tmp_file_name, &argc,
         EINA_FALSE);

   edje_pick_context_set(g->context);
   i = edje_pick_process(argc, argv);
   free(argv);

   if (i != EDJE_PICK_NO_ERROR)
     {
        _ok_popup_show(data, _cancel_popup, "Save Failed",
              (char *) edje_pick_err_str_get(i));

        free(tmp_file_name);
        return;
     }

   i = rename(tmp_file_name, g->file_name);
   if (i < 0)
     {
#define RENAME_ERR "renaming of '%s' to '%s' failed."
        char *err = malloc(strlen(RENAME_ERR) +
              strlen(tmp_file_name) +
              strlen(g->file_name) + 1);

        sprintf(err, RENAME_ERR, tmp_file_name, g->file_name);
        _ok_popup_show(data, _cancel_popup, "Rename Failed", err);
        free(err);
        free(tmp_file_name);
        return;
     }

   free(tmp_file_name);

   g->modified = EINA_FALSE;

   /* Clear UNDO / REDO  list each time we save file */
   _actions_list_clear(&(g->actions));

   _window_setting_update(g);
}

static void
_save_as_bt_do(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   gui_elements *g = data;
   if (g->inwin)
     {  /* Called for done, selected, free only once */
        evas_object_del(g->inwin);
        g->inwin = NULL;
     }

   if (event_info)
     {
        char **argv = NULL;
        int i, argc = 0;

        argv = _command_line_args_make(g, NULL, g->argv0, event_info, &argc,
              EINA_FALSE);

        edje_pick_context_set(g->context);
        i = edje_pick_process(argc, argv);
        free(argv);

        if (i != EDJE_PICK_NO_ERROR)
          {
             _ok_popup_show(data, _cancel_popup, "Save as Failed",
                   (char *) edje_pick_err_str_get(i));
             return;
          }

        g->file_name = eina_stringshare_add(event_info);
        g->modified = EINA_FALSE;
        _window_setting_update(g);
     }
}

static void
_save_as_bt_clicked(void *data,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{

   gui_elements *g = data;
   _file_selector_show(g, _save_as_bt_do, EINA_TRUE);
}

static void
_quit_bt_clicked(void *data,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
#define QUIT_MSG "File '%s' was modified, quit anyway?"
   gui_elements *g = data;

   if (g->modified)
     {
        const char *name = (g->file_name) ?
           g->file_name : EDJE_PICK_NEW_FILE_NAME_STR;

        char *buf = malloc(strlen(QUIT_MSG) + strlen(name) + 1);
        sprintf(buf, QUIT_MSG, name);

        _ok_cancel_popup_show(g, "Quit", buf, "YES", "NO",
           _client_win_del, _cancel_popup);

        free(buf);
     }
   else
     {
        _client_win_del(g, NULL, NULL);
     }
#undef QUIT_MSG
}

static void
_include_bt_clicked(void *data,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{  /* Remove selected files, groups */
   gui_elements *g = data;
   _file_selector_show(g, _include_bt_do, EINA_FALSE);
}

/* START - Drag And Drop Support */
#define DLMR "\n"          /* Delimiter      */
#define FSTR "file"        /* File Prefix    */
#define LSTR "list"        /* List Prefix    */
#define GRPR "group"       /* Group Prefix   */
#define IMGR "image"       /* Image Prefix   */
#define SMPR "sample"      /* Sample Prefix  */
#define FNTR "font"        /* Font Prefix    */
static void
_drop_open_file(gui_elements *g, char *file_name)
{  /* Open a file when dropped on gl_dst genlist */
#define DISCARD_MSG "File '%s' was modified, Discard changes?"
#define RELOAD_MSG "File '%s' is open, reload?"
   if (g->popup)
     {
        evas_object_del(g->popup);
        g->popup = NULL;
     }

   if (file_name)
     {
        if (g->file_to_open)
          eina_stringshare_del(g->file_to_open);

        g->file_to_open = eina_stringshare_add(file_name);

        if (g->modified)
          {
             const char *name = (g->file_name) ?
                g->file_name : EDJE_PICK_NEW_FILE_NAME_STR;

             char *buf = malloc(strlen(DISCARD_MSG) + strlen(name) + 1);
             sprintf(buf, DISCARD_MSG, name);

             _ok_cancel_popup_show(g, "Discard Changes", buf, "YES", "NO",
                   _do_open, _cancel_popup);

             free(buf);
             return;
          }

        if (g->file_name && (!strcmp(g->file_name, file_name)))
          {  /* This file is already open */
             char *buf = malloc(strlen(RELOAD_MSG) + strlen(g->file_name) + 1);
             sprintf(buf, RELOAD_MSG, g->file_name);

             _ok_cancel_popup_show(g, "File is open", buf,
                   "RELOAD", "CANCEL",
                   _do_open, _cancel_popup);

             free(buf);
             return;
          }

        _do_open(g, NULL, NULL);
     }
#undef RELOAD_MSG
#undef DISCARD_MSG
}

static Evas_Object *
_gl_createicon(void *data, Evas_Object *win, Evas_Coord *xoff, Evas_Coord *yoff)
{
   Evas_Object *icon = NULL;
   Evas_Object *o = elm_object_item_part_content_get(data, "elm.swallow.icon");

   if (o)
     {
        int xm, ym, w, h;
        evas_object_geometry_get(o, NULL, NULL, &w, &h);
        evas_pointer_canvas_xy_get(evas_object_evas_get(o), &xm, &ym);
        if (xoff) *xoff = xm - (w/2);
        if (yoff) *yoff = ym - (h/2);
        icon = _item_icon_create(
              elm_object_item_data_get(data), win,
              (unsigned int *) &xm, (unsigned int *) &ym); /* Unused */
        evas_object_size_hint_align_set(icon,
              EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(icon,
              EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        if (xoff && yoff) evas_object_move(icon, *xoff, *yoff);
        evas_object_resize(icon, w, h);
     }

   return icon;
}

static Eina_List *
_gl_icons_get(void *data)
{  /* Start icons animation before actually drag-starts */
   int yposret = 0;

   Eina_List *l;

   Eina_List *icons = NULL;

   Evas_Coord xm, ym;
   evas_pointer_canvas_xy_get(evas_object_evas_get(data), &xm, &ym);
   Eina_List *items = eina_list_clone(elm_genlist_selected_items_get(data));
   Elm_Object_Item *glit = elm_genlist_at_xy_item_get(data,
         xm, ym, &yposret);
   if (glit)
     {  /* Add the item mouse is over to the list if NOT seleced */
        void *p = eina_list_search_unsorted(items, _item_ptr_cmp, glit);
        if (!p)
          items = eina_list_append(items, glit);
     }

   EINA_LIST_FOREACH(items, l, glit)
     {  /* Now add icons to animation window */
        Evas_Object *o = elm_object_item_part_content_get(glit,
              "elm.swallow.icon");

        if (o)
          {
             int x, y, w, h;
             Evas_Object *ic = _item_icon_create(
                   elm_object_item_data_get(glit), o,
                   (unsigned int *) &w, (unsigned int *) &h);

             evas_object_geometry_get(o, &x, &y, &w, &h);
             evas_object_size_hint_align_set(ic,
                   EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(ic,
                   EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

             evas_object_move(ic, x, y);
             evas_object_resize(ic, w, h);
             evas_object_show(ic);

             icons =  eina_list_append(icons, ic);
          }
     }

   eina_list_free(items);
   return icons;
}

static void
_gl_dragdone(void *data, Evas_Object *obj __UNUSED__, Eina_Bool doaccept)
{
   printf("<%s> <%d> data=<%s> doaccept=<%d>\n",
         __func__, __LINE__, (char *) data, doaccept);

   free(data);  /* Free DND string */
   return;
}

static const char *
_gl_get_drag_data(Evas_Object *obj, Elm_Object_Item *it)
{  /* Construct a string of dragged info, user frees returned string */
   printf("<%s> <%d>\n", __func__, __LINE__);
   char *drag_data = NULL;
   Eina_List *items = eina_list_clone(elm_genlist_selected_items_get(obj));
   if (it)
     {  /* Add the item mouse is over to the list if NOT seleced */
        void *t = eina_list_search_unsorted(items, _item_ptr_cmp, it);
        if (!t)
          items = eina_list_append(items, it);
     }

   if (items)
     {  /* Now we can actually compose string to send and start dragging */
        Eina_List *l;
        gl_item_info *info;
        const char *file_name;
        const char *str = NULL;
        char buf[64];
        int len = 0;

        sprintf(buf, "%d%c%p", getpid(), EDJE_PICK_DND_DELIM, obj);

        EINA_LIST_FOREACH(items, l , it)
          {  /* Accomulate total length of string */
             info = elm_object_item_data_get(it);
             file_name = (info->file_name) ?
                info->file_name : EDJE_PICK_NEW_FILE_NAME_STR;

             switch (info->type)
               {
                case EDJE_PICK_TYPE_FILE:
                     {
                        str = FSTR;
                        break;
                     }

                case EDJE_PICK_TYPE_LIST:
                   str = LSTR;
                   break;

                case EDJE_PICK_TYPE_GROUP:
                   str = GRPR;
                   break;

                case EDJE_PICK_TYPE_IMAGE:
                   str = IMGR;
                   break;

                case EDJE_PICK_TYPE_SAMPLE:
                   str = SMPR;
                   break;

                case EDJE_PICK_TYPE_FONT:
                   str = FNTR;
                   break;

                default:
                   str = NULL;
                   break;
               }

             if (str)
               {
                  len += strlen(file_name) +
                     strlen(str) + 4 /* Delimiters */ +
                     strlen(info->name) +
                     strlen(buf) +
                     strlen(DLMR);
               }
          }

        drag_data = calloc(1, (len + 1));  /* Allocate string */

        EINA_LIST_FOREACH(items, l , it)
          {  /* Compose string from items */
             info = elm_object_item_data_get(it);
             file_name = (info->file_name) ?
                info->file_name : EDJE_PICK_NEW_FILE_NAME_STR;

             switch (info->type)
               {
                case EDJE_PICK_TYPE_FILE:
                   str = FSTR;
                   break;

                case EDJE_PICK_TYPE_LIST:
                   str = LSTR;
                   break;

                case EDJE_PICK_TYPE_GROUP:
                   str = GRPR;
                   break;

                case EDJE_PICK_TYPE_IMAGE:
                   str = IMGR;
                   break;

                case EDJE_PICK_TYPE_SAMPLE:
                   str = SMPR;
                   break;

                case EDJE_PICK_TYPE_FONT:
                   str = FNTR;
                   break;

                default:
                   str = NULL;
                   break;
               }

             if (str)
               {  /* Concat the current string */
                  char *e = drag_data + strlen(drag_data);
                  sprintf(e, "%s%c%s%c%s%c%s%c%s",
                        file_name, EDJE_PICK_DND_DELIM,
                        str, EDJE_PICK_DND_DELIM,
                        info->name, EDJE_PICK_DND_DELIM,
                        buf, EDJE_PICK_DND_DELIM, DLMR);
               }
          }

        eina_list_free(items);
        printf("<%s> <%d> Sending <%s>\n", __func__, __LINE__, drag_data);
     }

   return (const char *) drag_data;  /* Freed in dragdone */
}

static Eina_Bool
_gl_dnd_default_anim_data_getcb(Evas_Object *obj,  /* The genlist object */
      Elm_Object_Item *it,
      Elm_Drag_User_Info *info)
{  /* This called before starting to drag, mouse-down was on it */
   info->format = ELM_SEL_FORMAT_TARGETS;
   info->createicon = _gl_createicon;
   info->createdata = it;
   info->icons = _gl_icons_get(obj);
   info->dragdone = _gl_dragdone;

   /* Now, collect data to send for drop from ALL selected items */
   /* Save list pointer to remove items after drop and free list on done */
   info->data = info->acceptdata = info->donecbdata =
      (char *) _gl_get_drag_data(obj, it);

   if (info->data)
     return EINA_TRUE;
   else
     return EINA_FALSE;
}

static Elm_Object_Item *
_gl_item_getcb(Evas_Object *obj, Evas_Coord x, Evas_Coord y, int *xposret __UNUSED__, int *yposret)
{  /* This function returns pointer to item under (x,y) coords */
   printf("<%s> <%d> obj=<%p>\n", __func__, __LINE__, obj);
   Elm_Object_Item *glit;
   glit = elm_genlist_at_xy_item_get(obj, x, y, yposret);
   if (glit)
     printf("over <%s>, glit=<%p> yposret %i\n",
           elm_object_item_part_text_get(glit, "elm.text"), glit, *yposret);
   else
     printf("over none, yposret %i\n", *yposret);
   return glit;
}

static char *
_edje_pick_dnd_item_info_get(char *str,
      char **file_name,
      Edje_Pick_Type *type,
      char **name,
      char **pid,
      char **gl_str)
{  /* Get item info: file, type, name */
   *file_name = *name = *gl_str = NULL;
   *type = EDJE_PICK_TYPE_UNDEF;

   if (strchr(str, EDJE_PICK_DND_DELIM))
     {  /* The case of Drag and Drop within the applicaiton */
        char *t;
        char dlm[2];
        sprintf(dlm, "%c", EDJE_PICK_DND_DELIM);
        *file_name = strtok(str, dlm);
        t = strtok(NULL, dlm);
        *name = strtok(NULL, dlm);
        *pid = strtok(NULL, dlm);
        *gl_str = strtok(NULL, dlm);

        *type = EDJE_PICK_TYPE_UNDEF;
        if (t)
          {
             if (!strcmp(t, FSTR)) *type = EDJE_PICK_TYPE_FILE;
             else if (!strcmp(t, LSTR)) *type = EDJE_PICK_TYPE_LIST;
             else if (!strcmp(t, GRPR)) *type = EDJE_PICK_TYPE_GROUP;
             else if (!strcmp(t, IMGR)) *type = EDJE_PICK_TYPE_IMAGE;
             else if (!strcmp(t, SMPR)) *type = EDJE_PICK_TYPE_SAMPLE;
             else if (!strcmp(t, FNTR)) *type = EDJE_PICK_TYPE_FONT;
          }

        if (*gl_str)
          {  /* skip the new-line if there, otherwise no more input */
             t = strstr(((*gl_str) + strlen(*gl_str) + 1), DLMR);
             t = (t) ? (t + 1) : NULL;
          }

        return t;
     }
   else
     {  /* The case of getting file-names seperated by '\n' from system */
        char *p = strstr(str, DLMR);
        if (!p)
          p =  strstr(str, EDJE_PICK_SYS_DND_POSTFIX);
        if (!p)
          return NULL;
        else
          {
             *file_name = str;
             *p = '\0';
             printf("<%s> file_name=<%s>\n", __func__, *file_name);
             return (p + 1);
          }
     }
}

static Eina_Bool
_gl_dropcb(void *data, Evas_Object *obj,
      Elm_Object_Item *it EINA_UNUSED,
      Elm_Selection_Data *ev,
      int xposret EINA_UNUSED,
      int yposret EINA_UNUSED)
{  /* This function is called when data is dropped on the genlist */
   gui_elements *g = data;
   char *str = strdup(ev->data);  /* We change the string, make a copy */
   char *p = strstr(str, EDJE_PICK_SYS_DND_PREFIX);
   printf("<%s> <%d> str=<%s>\n", __func__, __LINE__, str);

   if (p)
     {  /* Found prefix, check string */
        char *file_name;
        Edje_Pick_Type type;
        char *name;
        char *gl_str;
        char *pid;
        Eina_List *s = NULL;
        Evas_Object *df = NULL;  /* Dragged From */
        p += strlen(EDJE_PICK_SYS_DND_PREFIX);

        do
          {  /* Check input and do the drop */
             p = _edje_pick_dnd_item_info_get
                (p, &file_name, &type, &name, &pid, &gl_str);

             if (type == EDJE_PICK_TYPE_UNDEF)
               {  /* Dropping a file from system */
                  if (file_name)
                    {
                       if (obj == g->gl_src)
                         _include_bt_do(g, NULL, file_name);
                       else if (obj == g->gl_dst)
                         _drop_open_file(g, file_name);
                    }
               }
             else
               {  /* Drop from application */
                  /* First compose a list of gl items from dopped-items */
                  if (file_name && name && gl_str)
                    {  /* Input is valid, add genlist item */
                       char buf[16];
                       sprintf(buf, "%p", g->gl_src);
                       if (!strcmp(buf, gl_str))
                         df = g->gl_src;
                       else
                         {
                            sprintf(buf, "%p", g->gl_dst);
                            if (!strcmp(buf, gl_str))
                              df = g->gl_dst;
                         }

                       if (atoi(pid) != getpid())
                         {  /* We don't alllow DND from other app */
#define F_DROP_ERR_1 "Cannot Drag and Drop from other '%s' app."
                            char *err = malloc(strlen(F_DROP_ERR_1) +
                                  strlen(g->argv0) + 1);

                            sprintf(err, F_DROP_ERR_1, g->argv0);
                            _ok_popup_show(data,
                                  _cancel_popup, "Drop Error", err);

                            printf("<%s> %s.\n", __func__, err);
                            free(err);
                            free(str);
                            return EINA_FALSE;
#undef F_DROP_ERR_1
                         }

                       if (df == obj)
                         {
#define F_DROP_ERR_2 "Cannot drop on source."
                            _ok_popup_show(data,
                                  _cancel_popup, "Drop Error", F_DROP_ERR_2);

                            printf("<%s> %s.\n", __func__, F_DROP_ERR_2);
                            free(str);
                            return EINA_FALSE;
#undef F_DROP_ERR_2
                         }
                       else
                         {  /* Ready to locate genlist item and add to s */
                            Elm_Object_Item *glit = _glit_node_find_by_info
                               (df, file_name, type, name);

                            if (glit)
                              s = eina_list_append(s,
                                    elm_object_item_data_get(glit));
                         }
                    }
                  else
                    {
                       printf("<%s> Invalide item file_name=<%s> name=<%s> gl_str=<%s> --- SKIPPED ---\n", __func__, file_name ,name, gl_str);
                    }

               }
          }while (p);

        /* First check OK to move groups */
        if (s)
          {
             Edje_Pick_Status status = EDJE_PICK_NO_ERROR;
             if (obj == g->gl_dst)
               {  /* Parse when dropped on dest */
                  int argc = 0;
                  char **argv = _command_line_args_make(g, s, g->argv0,
                        "/tmp/out.tmp", &argc, EINA_TRUE);

                  edje_pick_context_set(g->context);
                  status = edje_pick_command_line_parse(argc, argv,
                        NULL, NULL, EINA_TRUE);

                  free(argv);
               }

             if (status != EDJE_PICK_NO_ERROR)
               {  /* Parse came back with an error */
                  printf("%s\n", edje_pick_err_str_get(status));
                  _ok_popup_show(data, _cancel_popup, "Drop Error",
                        (char *) edje_pick_err_str_get(status));
               }
             else
               {  /* Add the selection to dest list */
                  printf("\n\nParse OK\n");
                  _edje_pick_items_move(g, df, obj, s, EINA_TRUE);
               }

             eina_list_free(s);
          }
     }

   free(str);
   return EINA_TRUE;
}
#undef DLMR
#undef FSTR
#undef LSTR
#undef GRPR
#undef IMGR
#undef SMPR
#undef FNTR
/* END   - Drag And Drop Support */

static int
_g_selected_types_count(Evas_Object *gl, int *types)
{  /* Returns number of items selected for each type */
   Eina_List *l;
   Elm_Object_Item *it;
   int i;
   int n = 0;

   memset(types, 0, sizeof(int) * EDJE_PICK_TYPE_LAST);
   const Eina_List *slct = elm_genlist_selected_items_get(gl);
   EINA_LIST_FOREACH((Eina_List *) slct, l, it)
     {
        gl_item_info *info = elm_object_item_data_get(it);
        Edje_Pick_Type type = info->type;
        switch (type)
          {
           case EDJE_PICK_TYPE_LIST:
              if (strcmp(info->name, EDJE_PICK_GROUPS_STR))
                break;

              /* Count Groups-List as a group, drop all others */
              type = EDJE_PICK_TYPE_GROUP;

           case EDJE_PICK_TYPE_GROUP:
           case EDJE_PICK_TYPE_IMAGE:
           case EDJE_PICK_TYPE_SAMPLE:
           case EDJE_PICK_TYPE_FONT:
              types[type]++;
              break;

           default:
              break;
          }
     }

   printf("<%s> (groups,images,samples,fonts)=(%d,%d,%d,%d)\n",__func__, types[EDJE_PICK_TYPE_GROUP],types[EDJE_PICK_TYPE_IMAGE],types[EDJE_PICK_TYPE_SAMPLE],types[EDJE_PICK_TYPE_FONT]);

   /* Count how many different types were involved */
   for (i = EDJE_PICK_TYPE_GROUP; i < EDJE_PICK_TYPE_LAST; i++)
     if (types[i]) n++;

   return n;
}

static void
_gui_keys_reset(void *data,
      Evas_Object *obj,
      void *event_info)
{
   gui_elements *g = data;
   gl_item_info *info = elm_object_item_data_get(event_info);
   int src_types[EDJE_PICK_TYPE_LAST];
   int dst_types[EDJE_PICK_TYPE_LAST];
   int i;
   int n_src =_g_selected_types_count(g->gl_src, src_types);
   int n_dst = _g_selected_types_count(g->gl_dst,dst_types);
   Eina_Bool take_bt_disable = EINA_FALSE;
   Eina_Bool remove_bt_disable = EINA_FALSE;

   /* Same values should be 'turned on' in both arrays */
   Eina_Bool xval = EINA_FALSE;
   for (i = EDJE_PICK_TYPE_GROUP; i < EDJE_PICK_TYPE_LAST; i++)
     xval |= (src_types[i]) ^ (dst_types[i]);

   if (n_src == 1)
     {
        if (src_types[EDJE_PICK_TYPE_GROUP] == 0)
          {
             take_bt_disable = (n_dst > 1) | xval;
          }
     }
   else
     take_bt_disable = EINA_TRUE;

   if (n_dst == 1)
     {
        if (dst_types[EDJE_PICK_TYPE_GROUP] == 0)
          {
             remove_bt_disable = (n_src > 1) | xval;
          }
     }
   else
     remove_bt_disable = EINA_TRUE;

   elm_object_item_disabled_set(g->actions.take_bt, take_bt_disable);
   elm_object_item_disabled_set(g->actions.remove_bt, remove_bt_disable);
}

static void
_gl_item_selected(void *data,
      Evas_Object *obj,
      void *event_info)
{
   gui_elements *g = data;
   gl_item_info *info = elm_object_item_data_get(event_info);
   printf("<%s> gl=<%p> selected <%s>\n", __func__, obj, info->name);
   _gui_keys_reset(data, obj, event_info);
}

static void
_gl_item_unselected(void *data,
      Evas_Object *obj,
      void *event_info)
{
   gui_elements *g = data;
   gl_item_info *info = elm_object_item_data_get(event_info);
   printf("<%s> gl=<%p> selected <%s>\n", __func__, obj, info->name);
   _gui_keys_reset(data, obj, event_info);
}

static void
left_pane_create(gui_elements *g)
{
   Evas_Object *hbx;
   g->bx_left = elm_box_add(g->panes);
   evas_object_size_hint_align_set(g->bx_left, 0, 0);
   evas_object_size_hint_weight_set(g->bx_left,
                                    EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

   hbx = elm_box_add(g->bx);
   elm_box_padding_set(hbx, 10, 0);
   elm_box_pack_end(g->bx_left, hbx);
   elm_box_horizontal_set(hbx, EINA_TRUE);

   /* Create the Genlist of source-file groups */
   g->gl_src = elm_genlist_add(g->bx_left);
   elm_genlist_multi_select_set(g->gl_src, EINA_TRUE);
   elm_box_pack_end(g->bx_left, g->gl_src);
   evas_object_size_hint_align_set(g->gl_src,
         EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(g->gl_src, 100.0, 100.0);

   evas_object_smart_callback_add(g->gl_src, "selected", _gl_item_selected, g);
   evas_object_smart_callback_add(g->gl_src, "unselected", _gl_item_unselected, g);
   evas_object_smart_callback_add(g->gl_src, "expand,request", gl_exp_req, g);
   evas_object_smart_callback_add(g->gl_src, "contract,request", gl_con_req, g);
   evas_object_smart_callback_add(g->gl_src, "expanded", gl_exp, g);
   evas_object_smart_callback_add(g->gl_src, "contracted", gl_con, g);

   elm_drop_item_container_add(g->gl_src,
         ELM_SEL_FORMAT_TARGETS,
         _gl_item_getcb,
         NULL, NULL,
         NULL, NULL,
         NULL, NULL,
         _gl_dropcb, g);

   elm_drag_item_container_add(g->gl_src,
         EDJE_PICK_ANIM_TIME,
         EDJE_PICK_DRAG_TIMEOUT,
         _gl_item_getcb,
         _gl_dnd_default_anim_data_getcb);

   evas_object_show(g->gl_src);
   evas_object_show(hbx);
   evas_object_show(g->bx_left);
   elm_object_part_content_set(g->panes, "left", g->bx_left);
}

static void
right_pane_create(gui_elements *g)
{
   Evas_Object *hbx;
   g->bx_right = elm_box_add(g->panes);
   evas_object_size_hint_align_set(g->bx_right, 0, 0);
   evas_object_size_hint_weight_set(g->bx_right,
         EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

   hbx = elm_box_add(g->bx);
   elm_box_padding_set(hbx, 10, 0);
   elm_box_pack_end(g->bx_right, hbx);
   elm_box_horizontal_set(hbx, EINA_TRUE);

   /* Create the Genlist of dest-file groups */
   g->gl_dst = elm_genlist_add(g->bx_right);
   elm_genlist_multi_select_set(g->gl_dst, EINA_TRUE);
   elm_box_pack_end(g->bx_right, g->gl_dst);
   evas_object_size_hint_align_set(g->gl_dst,
         EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(g->gl_dst, 100.0, 100.0);

   evas_object_smart_callback_add(g->gl_dst, "selected", _gl_item_selected, g);
   evas_object_smart_callback_add(g->gl_dst, "unselected", _gl_item_unselected, g);
   evas_object_smart_callback_add(g->gl_dst, "expand,request", gl_exp_req, g);
   evas_object_smart_callback_add(g->gl_dst, "contract,request", gl_con_req, g);
   evas_object_smart_callback_add(g->gl_dst, "expanded", gl_exp, g);
   evas_object_smart_callback_add(g->gl_dst, "contracted", gl_con, g);

   elm_drop_item_container_add(g->gl_dst,
         ELM_SEL_FORMAT_TARGETS,
         _gl_item_getcb,
         NULL, NULL,
         NULL, NULL,
         NULL, NULL,
         _gl_dropcb, g);

   elm_drag_item_container_add(g->gl_dst,
         EDJE_PICK_ANIM_TIME,
         EDJE_PICK_DRAG_TIMEOUT,
         _gl_item_getcb,
         _gl_dnd_default_anim_data_getcb);

   evas_object_show(g->gl_dst);
   evas_object_show(hbx);
   evas_object_show(g->bx_right);
   elm_object_part_content_set(g->panes, "right", g->bx_right);
}

int
main(int argc, char **argv)
{  /* Create Client Window */
   gui_elements *gui = _gui_alloc();
   Elm_Object_Item *tb_it;
   Evas_Object *file_menu;

   elm_init(argc, argv);
   gui->argv0 = eina_stringshare_add(argv[0]);

   gui->win = elm_win_util_standard_add("client", CLIENT_NAME);
   elm_win_autodel_set(gui->win, EINA_TRUE);


   gui->bx = elm_box_add(gui->win);
   evas_object_size_hint_weight_set(gui->bx,
         EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(gui->win, gui->bx);

   /* Top menu toolbar */
   gui->tb = elm_toolbar_add(gui->win);
   elm_toolbar_shrink_mode_set(gui->tb, ELM_TOOLBAR_SHRINK_NONE);
   elm_toolbar_select_mode_set(gui->tb, ELM_OBJECT_SELECT_MODE_ALWAYS);
   elm_object_style_set(gui->tb, "item_centered");
   evas_object_size_hint_weight_set(gui->tb, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(gui->tb, EVAS_HINT_FILL, 0.0);

   tb_it = elm_toolbar_item_append(gui->tb, NULL, "Menu", NULL, NULL);
   elm_toolbar_item_selected_set(tb_it, EINA_FALSE);
   elm_toolbar_item_menu_set(tb_it, EINA_TRUE);
   elm_toolbar_menu_parent_set(gui->tb, gui->win);
   file_menu = elm_toolbar_item_menu_get(tb_it);
   gui->menu_open = elm_menu_item_add(file_menu, NULL, NULL,
         "Open", _open_bt_clicked, gui);

   gui->menu_close = elm_menu_item_add(file_menu, NULL, NULL,
         "Close", _close_bt_clicked, gui);

   gui->menu_save = elm_menu_item_add(file_menu, NULL, NULL,
         "Save", _save_bt_clicked, gui);

   gui->menu_save_as = elm_menu_item_add(file_menu, NULL, NULL,
         "Save As", _save_as_bt_clicked, gui);

   gui->menu_quit = elm_menu_item_add(file_menu, NULL, NULL,
         "Quit", _quit_bt_clicked, gui);

   tb_it = elm_toolbar_item_append(gui->tb, NULL, "INCLUDE",
         _include_bt_clicked, gui);
   elm_toolbar_item_selected_set(tb_it, EINA_FALSE);

   gui->actions.take_bt = elm_toolbar_item_append(gui->tb, NULL, ">>",
         _take_bt_clicked, gui);
   elm_object_item_disabled_set(gui->actions.take_bt, EINA_TRUE);
   elm_toolbar_item_selected_set(gui->actions.take_bt, EINA_FALSE);

   gui->actions.remove_bt = elm_toolbar_item_append(gui->tb, NULL, "<<",
         _remove_bt_clicked, gui);
   elm_object_item_disabled_set(gui->actions.remove_bt, EINA_TRUE);
   elm_toolbar_item_selected_set(gui->actions.remove_bt, EINA_FALSE);

   gui->actions.undo_bt = elm_toolbar_item_append(gui->tb, NULL, "UNDO",
         _undo_bt_clicked, gui);
   elm_object_item_disabled_set(gui->actions.undo_bt, EINA_TRUE);
   elm_toolbar_item_selected_set(gui->actions.undo_bt, EINA_FALSE);

   gui->actions.redo_bt = elm_toolbar_item_append(gui->tb, NULL, "REDO",
         _redo_bt_clicked, gui);
   elm_object_item_disabled_set(gui->actions.redo_bt, EINA_TRUE);
   elm_toolbar_item_selected_set(gui->actions.redo_bt, EINA_FALSE);

   elm_box_pack_end(gui->bx, gui->tb);
   evas_object_show(gui->tb);

   gui->panes = elm_panes_add(gui->bx);
   evas_object_size_hint_weight_set(gui->panes, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(gui->panes, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(gui->bx, gui->panes);
   evas_object_show(gui->panes);

   /* Group-Genlist item creation funcs */
   gui->itc.item_style = "default";
   gui->itc.func.text_get = _group_item_text_get;
   gui->itc.func.content_get = _group_item_icon_get;
   gui->itc.func.state_get = NULL;
   gui->itc.func.del = NULL;

   /* Group-Genlist item creation funcs */
   gui->itc_group.item_style = "group_index";
   gui->itc_group.func.text_get = _group_item_text_get;
   gui->itc_group.func.content_get = _group_item_icon_get;
   gui->itc_group.func.state_get = NULL;
   gui->itc_group.func.del = NULL;

   left_pane_create(gui);
   right_pane_create(gui);

   /* Resize and show main window */
   evas_object_resize(gui->win, 500, 500);
   evas_object_show(gui->bx);
   evas_object_show(gui->win);

   evas_object_smart_callback_add(gui->win, "delete,request", _client_win_del, gui);
printf("<%s> gl_src=<%p>, gl_dst=<%p>\n", __func__, gui->gl_src, gui->gl_dst);

   _window_setting_update(gui);

   edje_pick_init();
   elm_run();
   edje_pick_shutdown();

   edje_pick_context_set(NULL);

   /* cleanup - free files data */
   elm_shutdown();

   return 0;
}
