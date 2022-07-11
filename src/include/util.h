#include "flux-types.h"

void die(char *name);
void graceful_shutdown(char *name, struct data server); // has to use the fds, cannot use FILE*
