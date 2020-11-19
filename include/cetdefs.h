/* CET's FineIBT will require certain functions to be coarse-grained.
   Define the attribute for such if -fcf-protection=fine */
#if defined __CET__ && __CET__ & 0x10
# define _COARSECF_CHECK __attribute__((coarsecf_check))
#else
# define _COARSECF_CHECK
#endif
