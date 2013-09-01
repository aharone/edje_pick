#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <Elementary.h>
#include "edje_private.h"
#include "Edje_Pick.h"

#define CLIENT_NAME         "Edje-Pick Client"

#define DRAG_TIMEOUT 0.3
#define ANIM_TIME 0.5

#define EDJE_PICK_GROUPS_STR  "Groups"
#define EDJE_PICK_IMAGES_STR  "Images"
#define EDJE_PICK_SAMPLES_STR "Samples"
#define EDJE_PICK_FONTS_STR   "Fonts"

#define EDJE_PICK_NEW_FILE_NAME_STR "Untitled"
enum _Edje_Pick_Type
{
   EDJE_PICK_TYPE_UNDEF,  /* 0 - if not intialized */
   EDJE_PICK_TYPE_FILE,   /* Node is file head */
   EDJE_PICK_TYPE_LIST,   /* Node contains list of "Groups", "Images", ... */
   EDJE_PICK_TYPE_GROUP,  /* Node contains a group name */
   EDJE_PICK_TYPE_IMAGE,  /* Node contains a image name */
   EDJE_PICK_TYPE_SAMPLE, /* Node contains a sound-sample name */
   EDJE_PICK_TYPE_FONT    /* Node contains a font name */
};
typedef enum _Edje_Pick_Type Edje_Pick_Type;

/* gl info as follows:
   file_name = full-path file name for all nodes.
   if its genlist-group data struct will contain:
   name = full-path file name.
   sub = list items named: "Groups", "Images", "Samples", "Fonts"

   For each group will have a node both its name and sub-items list.
   For each sub-item just fill-in name and set sub to NULL */
struct _gl_item_info
{
   const char *file_name;  /* From what file it comes */
   Edje_Pick_Type type;    /* Specifys what type of data struct contains */
   const char *name;       /* Item name to display */
   Eina_List *sub;         /* Not NULL for item represent head of tree */
};
typedef struct _gl_item_info gl_item_info;

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
   Edje_Pick context;
};
typedef struct _gui_elements gui_elements;

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

gui_elements *
_gui_alloc(void)
{  /* Will do any complex-allocation proc here */
   return calloc(1, sizeof(gui_elements));
}

static void
_gui_free(gui_elements *g)
{

   if (g->file_to_open)
     eina_stringshare_del(g->file_to_open);

   if (g->file_name)
     eina_stringshare_del(g->file_name);

   _gl_data_free(g->gl_src);
   _gl_data_free(g->gl_dst);

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

static Evas_Object *
_group_item_icon_get(void *data EINA_UNUSED, Evas_Object *parent EINA_UNUSED,
      const char *part EINA_UNUSED)
{  /* TODO: Set icon according to type field */
   if (!strcmp(part, "elm.swallow.icon"))
     {
        gl_item_info *info = data;
        char *file = "/home/aharon/e17/edje_pick/icons/group.png";
        Evas_Object *icon = elm_icon_add(parent);
        if (info->sub)
          file = "/home/aharon/e17/edje_pick/icons/file.png";

        elm_image_file_set(icon, file, NULL);
        evas_object_size_hint_aspect_set(icon, EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
        evas_object_show(icon);
        return icon;
     }

   return NULL;
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

        elm_genlist_item_append(obj, itc, treeit, glit, iflag, NULL, NULL);
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
        Eina_Bool fn = (file) ? (!strcmp(file, tmp->file_name)) : EINA_TRUE;

        if (((tmp->type == EDJE_PICK_TYPE_LIST) &&
                 (!strcmp(tmp->name, name))) && fn)
          return glit;

        glit = elm_genlist_item_next_get(glit);
     }

   return NULL;
}

static Elm_Object_Item *
_glit_node_find(Evas_Object *gl, gl_item_info *info)
{
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
_ok_popup_show(gui_elements *g, Evas_Smart_Cb func, char *title, char *err)
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
        Edje_File *edf;
        Eina_Iterator *i;
        char *name = NULL;
        Elm_Object_Item *file_glit = NULL;
        Elm_Object_Item *groups_glit = NULL;
        gl_item_info *file_info = NULL;
        gl_item_info *group_info = NULL;
        char *err = NULL;
        Eet_File *ef = eet_open(file_name, EET_FILE_MODE_READ);
        if (!ef)
          {
#define F_OPEN_ERR "Failed to open file <%s>"
             err = malloc(strlen(F_OPEN_ERR) +
                   strlen(file_name) + 1);

             sprintf(err, F_OPEN_ERR, file_name);
             _ok_popup_show(data, _cancel_popup, "File Error", err);
             free(err);
             printf("<%s> Failed to open file <%s>\n", __func__, file_name);
             return file_glit;
          }

        edf = eet_data_read(ef, _edje_edd_edje_file, "edje/file");
        if (!edf)
          {
#define F_READ_ERR "Failed to read file <%s>"
             eet_close(ef);
             err = malloc(strlen(F_READ_ERR) +
                   strlen(file_name) + 1);

             sprintf(err, F_READ_ERR, file_name);
             _ok_popup_show(data, _cancel_popup, "File Error", err);
             free(err);
             printf("<%s> Failed to read file <%s>\n", __func__, file_name);
             return file_glit;
          }

        if (!(edf->collection))
          {  /* This will handle the case of empty, corrupted file */
#define F_COLLECT_ERR "File collection is empty (corrupted?) <%s>"
             eet_close(ef);
             err = malloc(strlen(F_COLLECT_ERR) +
                   strlen(file_name) + 1);

             sprintf(err, F_COLLECT_ERR, file_name);
             _ok_popup_show(data, _cancel_popup, "File Error", err);
             free(err);
             printf("<%s> File collection is empty (corrupted?) <%s>\n"
                   ,__func__, file_name);
             return file_glit;
          }

        if (gl == g->gl_src)
          {  /* Allocate file-info only if this is source genlist */
             file_info = calloc(1, sizeof(gl_item_info));
             file_info->file_name = eina_stringshare_add(file_name);
             file_info->type = EDJE_PICK_TYPE_FILE;
             file_info->name = eina_stringshare_add(file_name);
          }

        i = eina_hash_iterator_key_new(edf->collection);
        EINA_ITERATOR_FOREACH(i, name)
          {  /* Create all groups (if any) as children */
             if (!group_info)
               {
                  group_info = calloc(1, sizeof(gl_item_info));
                  group_info->file_name = eina_stringshare_add(file_name);
                  group_info->type = EDJE_PICK_TYPE_LIST;
                  group_info->name = eina_stringshare_add(EDJE_PICK_GROUPS_STR);
               }

             gl_item_info *child = calloc(1, sizeof(gl_item_info));
             child->file_name = eina_stringshare_add(file_name);
             child->type = EDJE_PICK_TYPE_GROUP;
             child->name = eina_stringshare_add(name);
             group_info->sub = eina_list_append(group_info->sub, child);
          }

        if (file_info)
          {  /* only if gl is gl_src */
             file_info->sub = eina_list_append(file_info->sub, group_info);
             file_glit = elm_genlist_item_append(gl, itc_group,
                   file_info, NULL,
                   ELM_GENLIST_ITEM_GROUP, NULL, NULL);
          }

        if (group_info)
          groups_glit = elm_genlist_item_append(gl, itc,
                group_info, file_glit,
                ELM_GENLIST_ITEM_TREE, NULL, NULL);

        elm_genlist_item_expanded_set(groups_glit, EINA_TRUE);

        eina_iterator_free(i);
        eet_close(ef);

        if (gl == g->gl_dst)
          {
             if (g->file_name)
               eina_stringshare_del(g->file_name);

             g->file_name = eina_stringshare_add(file_name);
             g->modified = EINA_FALSE;
             _window_setting_update(g);
          }

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

#define ADD_ARG(LIST, STR, CNT) do{ \
     LIST = eina_list_append(LIST, STR); \
     CNT++; \
}while(0);

static char **
_command_line_args_make(gui_elements *g, Eina_List *s,
      const char *prog, const char *outfile, int *argc,
      Eina_Bool strict)
{  /* User has to free returned char ** */
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

   EINA_LIST_FOREACH(s, l, it)
     {  /* Run through selected-items and build args-list */
        info = elm_object_item_data_get(it);
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

   elm_genlist_item_expanded_set(it, EINA_TRUE);
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
        elm_genlist_item_update(file_glit);
     }

   it = elm_genlist_item_append(gl, &(g->itc),
         group_head, file_glit,
         EINA_TRUE, NULL, NULL);

   elm_genlist_item_expanded_set(it, EINA_TRUE);
   return it;
}

static void
_leaf_item_move(gui_elements *g,
      Evas_Object *src, Evas_Object *dst, gl_item_info *info)
{
   Elm_Object_Item *it = _glit_node_find(src, info);
   Elm_Object_Item *pit = NULL;
   const char *file = (dst == g->gl_src) ? (info->file_name) : NULL;

   if (!_gl_data_find(dst, file, info->type, info->name))
     {  /* Add leaf-data to dest genlist */
        gl_item_info *list_info = NULL;
        char *list_str = _list_string_get(info->type);
        Elm_Object_Item *ithd = _glit_head_list_node_find(dst, file, list_str);
        if (!ithd) /* Make list node and parent if needed */
          ithd = _list_item_add(g, dst, file, list_str, dst == g->gl_src);

        list_info = elm_object_item_data_get(ithd);
        list_info->sub = eina_list_append(list_info->sub, info);
        elm_genlist_item_append(dst, &(g->itc), info, ithd,
              ELM_GENLIST_ITEM_NONE, NULL, NULL);
        elm_genlist_item_update(ithd);
     }

   if (it)
     {
        pit = elm_genlist_item_parent_get(it);
        elm_object_item_del(it);
     }

   if (pit)
     {
        gl_item_info *pinfo = elm_object_item_data_get(pit);
        pinfo->sub = eina_list_remove(pinfo->sub, info);
     }
}

static Eina_List *
_list_item_move(gui_elements *g, Eina_List *del,
      Evas_Object *src, Evas_Object *dst, gl_item_info *info)
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
      _leaf_item_move(g, src, dst, tmp);

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
      Evas_Object *src, Evas_Object *dst, gl_item_info *info)
{  /* Remove file-node from source in found and copy all sub-lists */
   Eina_List *l, *l_next;
   gl_item_info *tmp;
   Elm_Object_Item *it = _glit_head_file_node_find(src, info->name);
   if (it)
     elm_object_item_del(it);

   EINA_LIST_FOREACH_SAFE(info->sub, l, l_next, tmp)
      del = _list_item_move(g, del, src, dst, tmp);

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
      Eina_List *s)
{  /* Move all items in s from src to dest */
   Eina_List *items = NULL;
   Eina_List *l;
   Elm_Object_Item *it;
   gl_item_info *info;
   Eina_List *deleted_infos = NULL;
   EINA_LIST_FOREACH(s, l, it)
     {  /* Build a list of all selected-items infos */
        items = eina_list_append(items, elm_object_item_data_get(it));
     }

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
                         src, dst, info);
                   break;
                }

           case EDJE_PICK_TYPE_LIST:
                {
                   deleted_infos = _list_item_move(g, deleted_infos,
                         src, dst, info);
                   break;
                }

           case EDJE_PICK_TYPE_GROUP:
           case EDJE_PICK_TYPE_IMAGE:
           case EDJE_PICK_TYPE_SAMPLE:
           case EDJE_PICK_TYPE_FONT:
                {
                   _leaf_item_move(g, src, dst, info);
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

   g->modified = EINA_TRUE;
   _window_setting_update(g);
}

static void
_take_bt_clicked(void *data EINA_UNUSED,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{  /* Copy selected items */
   gui_elements *g = data;

   Eina_List *s = eina_list_clone(elm_genlist_selected_items_get(g->gl_src));

   /* First check OK to move groups */
   if (s)
     {
        /* Used for the parse call */
        Eina_List *ifs = NULL;
        char *ofn = NULL;
        int argc = 0;
        Edje_Pick_Status status = EDJE_PICK_NO_ERROR;
        char **argv = _command_line_args_make(g, s, g->argv0,
              "/tmp/out.tmp", &argc, EINA_TRUE);

        _edje_pick_context_set(&g->context);
        status = _edje_pick_command_line_parse(argc, argv, &ifs, &ofn);
        free(argv);
        if (status != EDJE_PICK_NO_ERROR)
          {  /* Parse came back with an error */
             printf("%s\n", _edje_pick_err_str_get(status));
             _ok_popup_show(data, _cancel_popup, "Error",
                   (char *) _edje_pick_err_str_get(status));
          }
        else
          {  /* Add the selection to dest list */
             printf("\n\nParse OK\n");
             _edje_pick_items_move(g, g->gl_src, g->gl_dst, s);
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

   Eina_List *s = eina_list_clone(elm_genlist_selected_items_get(g->gl_dst));

   if (s)
     {
        _edje_pick_items_move(g, g->gl_dst, g->gl_src, s);
        eina_list_free(s);
     }

   return;
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

   _edje_pick_context_set(&g->context);
   i = _edje_pick_process(argc, argv);
   free(argv);

   if (i != EDJE_PICK_NO_ERROR)
     {
        _ok_popup_show(data, _cancel_popup, "Save Failed",
              (char *) _edje_pick_err_str_get(i));

        free(tmp_file_name);
        return;
     }

   i = rename(tmp_file_name, g->file_name);
   if (i < 0)
     {
#define RENAME_ERR "renaming of <%s> to <%s> failed."
        char *err = malloc(strlen(RENAME_ERR) +
              strlen(tmp_file_name) +
              strlen(g->file_name)) + 1;

        sprintf(err, RENAME_ERR, tmp_file_name, g->file_name);
        _ok_popup_show(data, _cancel_popup, "Rename Failed", err);
        free(err);
        free(tmp_file_name);
        return;
     }

   free(tmp_file_name);

   g->modified = EINA_FALSE;
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

        _edje_pick_context_set(&g->context);
        i = _edje_pick_process(argc, argv);
        free(argv);

        if (i != EDJE_PICK_NO_ERROR)
          {
             _ok_popup_show(data, _cancel_popup, "Save as Failed",
                   (char *) _edje_pick_err_str_get(i));
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
}

static void
_include_bt_clicked(void *data,
      Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{  /* Remove selected files, groups */
   gui_elements *g = data;
   _file_selector_show(g, _include_bt_do, EINA_FALSE);
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

   evas_object_smart_callback_add(g->gl_src, "expand,request", gl_exp_req, g);
   evas_object_smart_callback_add(g->gl_src, "contract,request", gl_con_req, g);
   evas_object_smart_callback_add(g->gl_src, "expanded", gl_exp, g);
   evas_object_smart_callback_add(g->gl_src, "contracted", gl_con, g);

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

   evas_object_smart_callback_add(g->gl_dst, "expand,request", gl_exp_req, g);
   evas_object_smart_callback_add(g->gl_dst, "contract,request", gl_con_req, g);
   evas_object_smart_callback_add(g->gl_dst, "expanded", gl_exp, g);
   evas_object_smart_callback_add(g->gl_dst, "contracted", gl_con, g);

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
   tb_it = elm_toolbar_item_append(gui->tb, NULL, ">>",
         _take_bt_clicked, gui);
   elm_toolbar_item_selected_set(tb_it, EINA_FALSE);
   tb_it = elm_toolbar_item_append(gui->tb, NULL, "<<",
         _remove_bt_clicked, gui);
   elm_toolbar_item_selected_set(tb_it, EINA_FALSE);

   tb_it = elm_toolbar_item_append(gui->tb, NULL, "UNDO", NULL, NULL);
   elm_toolbar_item_selected_set(tb_it, EINA_FALSE);
   tb_it = elm_toolbar_item_append(gui->tb, NULL, "REDO", NULL, NULL);
   elm_toolbar_item_selected_set(tb_it, EINA_FALSE);

   elm_box_pack_end(gui->bx, gui->tb);
   evas_object_show(gui->tb);

//   top_panel_create(gui);

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

   ecore_init();
   eina_init();
   eet_init();
   _edje_edd_init();
   _window_setting_update(gui);

   elm_run();

   _edje_pick_context_set(NULL);

   /* cleanup - free files data */
   _edje_edd_shutdown();
   eet_shutdown();
   elm_shutdown();

   return 0;
}
