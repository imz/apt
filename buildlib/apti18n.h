#ifndef APTI18N_H
#define APTI18N_H 1

// CNC:2003-03-21
#include <config.h>
#include <gettext.h>
#define _(x) gettext(x)

#define N_(x) gettext_noop(x)

#endif /* APTI18N_H */
