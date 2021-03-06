/*
 * config.h: Configurable definitions used throughout the project.
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#error _POSIX_C_SOURCE >= 200809L required.
#endif

#include <limits.h>

#ifndef CFG_PORT
#define CFG_PORT 49999 /* port to connect on */
#endif

#ifndef CFG_BACKLOG
#define CFG_BACKLOG 4 /* server backlog size */
#endif

#define CFG_MAXHOST 1025   /* maximum address host name length; `NI_MAXHOST`
                             requires `_DEFAULT_SOURCE` to be defined */
#define CFG_MAXLINE BUFSIZ  /* max length of a single input line */
#define CFG_PROMPT "mftp$ " /* string to prompt client user for a command */

#endif // CONFIG_H_
