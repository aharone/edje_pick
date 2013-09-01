#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "Edje_Pick.h"

int
main(int argc, char **argv)
{
   Edje_Pick *context = calloc(1, sizeof(Edje_Pick));
   _edje_pick_context_set(context);
   int status;

   eina_init();
   eet_init();
   ecore_init();
   _edje_edd_init();
   eina_log_level_set(EINA_LOG_LEVEL_WARN);  /* Changed to INFO if verbose */

   status = _edje_pick_process(argc, argv);

   free(context);
   _edje_edd_shutdown();
   eet_shutdown();
   return status;
}
