/*
 * Compatibility shims for building dcomp as a native Windows DLL with MinGW.
 * Replaces wine/debug.h macros with no-ops so the source compiles without
 * the Wine debug-channel infrastructure.
 */

#ifndef __WINE_COMPAT_H
#define __WINE_COMPAT_H

/* Suppress the Wine debug-channel declaration that appears at file scope. */
#define WINE_DEFAULT_DEBUG_CHANNEL(x)

/* All debug output macros become no-ops. */
#define TRACE(...)  ((void)0)
#define FIXME(...)  ((void)0)
#define ERR(...)    ((void)0)
#define WARN(...)   ((void)0)

/* String helpers used inside TRACE/FIXME/ERR format strings. */
static inline const char *debugstr_guid(const GUID *id) { (void)id; return ""; }
static inline const char *debugstr_w(const WCHAR *s)    { (void)s;  return ""; }
static inline const char *debugstr_a(const char *s)     { (void)s;  return ""; }

/* Other missing fns */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define STATUS_INVALID_PARAMETER ((HRESULT)0xC000000DL)
#define DCX_USESTYLE         0x00010000

#define DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED         _HRESULT_TYPEDEF_(0x88980800)
#define DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED          _HRESULT_TYPEDEF_(0x88980801)
#define DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED      _HRESULT_TYPEDEF_(0x88980802)

#endif /* __WINE_COMPAT_H */