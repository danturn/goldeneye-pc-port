#ifndef _ENVFLAG_H_
#define _ENVFLAG_H_

/* Cached env-var probes for hot paths (D250/D302: an uncached getenv() per
 * node/poly/audio event is a measurable per-frame cost on Windows). Each
 * expansion site gets its own static, read once on first use; the env is
 * fixed for the process lifetime, so this is exact. The TU must already have
 * a real getenv() prototype (see D324). GCC/Clang statement expressions. */
#define GE_ENVFLAG(name) __extension__({ \
    static int _ge_envflag = -1; \
    if (_ge_envflag < 0) _ge_envflag = getenv(name) != NULL; \
    _ge_envflag; })

#define GE_ENVSTR(name) __extension__({ \
    static int _ge_envstr_init = 0; \
    static const char *_ge_envstr; \
    if (!_ge_envstr_init) { _ge_envstr = getenv(name); _ge_envstr_init = 1; } \
    _ge_envstr; })

#endif
