/*
 * Compatibility shims for building dcomp as a native Windows DLL with MinGW.
 * Replaces wine/debug.h macros with no-ops so the source compiles without
 * the Wine debug-channel infrastructure.
 */

#ifndef __WINE_COMPAT_H
#define __WINE_COMPAT_H

/* Suppress the Wine debug-channel declaration that appears at file scope. */
#define WINE_DEFAULT_DEBUG_CHANNEL(x)

#ifdef DCOMP_DEBUG

/* Debug build: route messages to OutputDebugStringA, visible in Wine with
 * WINEDEBUG=+debugstr (timestamps/pid/tid are added by Wine). */
#include <stdio.h>

static inline void __attribute__((format(printf, 3, 4)))
dcomp_dbg_printf(const char *cls, const char *func, const char *fmt, ...)
{
    char buf[1024];
    int len;
    va_list args;

    len = snprintf(buf, sizeof(buf), "dcomp:%s:%s ", cls, func);
    va_start(args, fmt);
    vsnprintf(buf + len, sizeof(buf) - len, fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

/* Small ring of static buffers so several helpers can be used in one call. */
static inline char *dcomp_dbg_buffer(void)
{
    static char bufs[16][128];
    static LONG idx;
    return bufs[InterlockedIncrement(&idx) % 16];
}

static inline const char *debugstr_guid(const GUID *id)
{
    char *b;
    if (!id) return "(null)";
    b = dcomp_dbg_buffer();
    snprintf(b, 128, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             id->Data1, id->Data2, id->Data3, id->Data4[0], id->Data4[1], id->Data4[2],
             id->Data4[3], id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7]);
    return b;
}

static inline const char *wine_dbgstr_rect(const RECT *r)
{
    char *b;
    if (!r) return "(null)";
    b = dcomp_dbg_buffer();
    snprintf(b, 128, "(%ld,%ld)-(%ld,%ld)", r->left, r->top, r->right, r->bottom);
    return b;
}

static inline const char *debugstr_a(const char *s)
{
    char *b;
    if (!s) return "(null)";
    b = dcomp_dbg_buffer();
    snprintf(b, 128, "\"%s\"", s);
    return b;
}

static inline const char *debugstr_w(const WCHAR *s)
{
    char *b;
    if (!s) return "(null)";
    b = dcomp_dbg_buffer();
    snprintf(b, 128, "L\"%ls\"", s);
    return b;
}

#define wine_dbgstr_guid debugstr_guid

#define TRACE(...)  dcomp_dbg_printf("trace", __func__, __VA_ARGS__)
#define FIXME(...)  dcomp_dbg_printf("fixme", __func__, __VA_ARGS__)
#define ERR(...)    dcomp_dbg_printf("err", __func__, __VA_ARGS__)
#define WARN(...)   dcomp_dbg_printf("warn", __func__, __VA_ARGS__)

#else

/* All debug output macros become no-ops. */
#define TRACE(...)  ((void)0)
#define FIXME(...)  ((void)0)
#define ERR(...)    ((void)0)
#define WARN(...)   ((void)0)

/* String helpers used inside TRACE/FIXME/ERR format strings. */
static inline const char *debugstr_guid(const GUID *id) { (void)id; return ""; }
static inline const char *debugstr_w(const WCHAR *s)    { (void)s;  return ""; }
static inline const char *debugstr_a(const char *s)     { (void)s;  return ""; }

#endif /* DCOMP_DEBUG */

/* Other missing fns */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define STATUS_INVALID_PARAMETER ((HRESULT)0xC000000DL)
#define DCX_USESTYLE         0x00010000

#define DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED         _HRESULT_TYPEDEF_(0x88980800)
#define DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED          _HRESULT_TYPEDEF_(0x88980801)
#define DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED      _HRESULT_TYPEDEF_(0x88980802)

#endif /* __WINE_COMPAT_H */