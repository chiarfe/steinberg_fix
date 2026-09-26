/*
 * Native in-process replacements for Wine server DirectComposition shared visual calls.
 * Used only when WINE_NATIVE_BUILD is defined.
 *
 * The three server requests this replaces:
 *   dcomp_create_shared_visual      -> allocates a native_shared_visual on the heap
 *   dcomp_set_shared_visual_info    -> stores target_root in that allocation
 *   dcomp_get_shared_visual_info    -> retrieves target_root from that allocation
 *
 * The HANDLE returned by DCompositionCreateSharedVisualHandle is a heap pointer
 * cast via (UINT_PTR). CloseHandle on such a handle will fail silently; the
 * allocation is intentionally not freed (acceptable for a same-process stub).
 */

#ifndef __WINE_NATIVE_SERVER_H
#define __WINE_NATIVE_SERVER_H

#include <stdlib.h>
#include <string.h>

typedef unsigned __int64 client_ptr_t;
/* In the native stub handles carry full pointer values, so use 64-bit width. */
typedef unsigned __int64 obj_handle_t;
typedef unsigned int     user_handle_t;
typedef unsigned int     data_size_t;

/* ---- Request-type discriminator ---- */
enum server_request_type
{
    REQ_dcomp_create_shared_visual,
    REQ_dcomp_set_shared_visual_info,
    REQ_dcomp_get_shared_visual_info,
};

/*
 * Every request struct starts with __req_type (discriminator) and __reply
 * (pointer to the caller-allocated reply struct that wine_server_call fills).
 */

struct dcomp_create_shared_visual_request
{
    unsigned int  __req_type;
    void         *__reply;
};
struct dcomp_create_shared_visual_reply
{
    obj_handle_t  handle;
};

struct dcomp_set_shared_visual_info_request
{
    unsigned int  __req_type;
    void         *__reply;
    obj_handle_t  handle;
    client_ptr_t  target_root;
};
struct dcomp_set_shared_visual_info_reply
{
    int _unused;
};

struct dcomp_get_shared_visual_info_request
{
    unsigned int  __req_type;
    void         *__reply;
    obj_handle_t  handle;
};
struct dcomp_get_shared_visual_info_reply
{
    client_ptr_t  target_root;
};

/* ---- SERVER_START_REQ / SERVER_END_REQ ---- */
/*
 * Declares local request and reply structs, zeroes them, sets the discriminator
 * and the back-pointer so that wine_server_call() can write results into reply.
 */
#define SERVER_START_REQ(type) \
    do { \
        struct type##_request __req_data; \
        struct type##_reply   __reply_data; \
        struct type##_request       * const req   = &__req_data; \
        const  struct type##_reply  * const reply = &__reply_data; \
        memset(&__req_data,   0, sizeof(__req_data)); \
        memset(&__reply_data, 0, sizeof(__reply_data)); \
        req->__req_type = REQ_##type; \
        req->__reply    = &__reply_data; \
        do

#define SERVER_END_REQ \
        while(0); \
    } while(0)

/* ---- Heap-allocated state for each shared visual ---- */
struct native_shared_visual
{
    client_ptr_t target_root;
};

/* ---- wine_server_call: native in-process dispatch ---- */
static inline unsigned int wine_server_call(void *req_ptr)
{
    unsigned int req_type = *(unsigned int *)req_ptr;

    switch (req_type)
    {
    case REQ_dcomp_create_shared_visual:
    {
        struct dcomp_create_shared_visual_request *req = (struct dcomp_create_shared_visual_request *)req_ptr;
        struct dcomp_create_shared_visual_reply   *rep = (struct dcomp_create_shared_visual_reply *)req->__reply;
        struct native_shared_visual *sv = (struct native_shared_visual *)malloc(sizeof(*sv));
        if (!sv) return 0xC0000017u; /* STATUS_NO_MEMORY */
        sv->target_root = 0;
        rep->handle = (obj_handle_t)(UINT_PTR)sv;
        return 0;
    }
    case REQ_dcomp_set_shared_visual_info:
    {
        struct dcomp_set_shared_visual_info_request *req = (struct dcomp_set_shared_visual_info_request *)req_ptr;
        struct native_shared_visual *sv = (struct native_shared_visual *)(UINT_PTR)req->handle;
        sv->target_root = req->target_root;
        return 0;
    }
    case REQ_dcomp_get_shared_visual_info:
    {
        struct dcomp_get_shared_visual_info_request *req = (struct dcomp_get_shared_visual_info_request *)req_ptr;
        struct dcomp_get_shared_visual_info_reply   *rep = (struct dcomp_get_shared_visual_info_reply *)req->__reply;
        struct native_shared_visual *sv = (struct native_shared_visual *)(UINT_PTR)req->handle;
        rep->target_root = sv->target_root;
        return 0;
    }
    default:
        return 0xC000000Du; /* STATUS_INVALID_PARAMETER */
    }
}

/* convert an object handle to a server handle */
static inline obj_handle_t wine_server_obj_handle(HANDLE handle)
{
  if ((int)(INT_PTR)handle != (INT_PTR)handle)
    return 0xfffffff0; /* some invalid handle */
  return (INT_PTR)handle;
}

static inline user_handle_t wine_server_user_handle(HANDLE handle)
{
  return (UINT_PTR)handle;
}

/* convert a server handle to a generic handle */
static inline HANDLE wine_server_ptr_handle(obj_handle_t handle)
{
  return (HANDLE)(INT_PTR)(int)handle;
}

/* convert a client pointer to a server client_ptr_t */
static inline client_ptr_t wine_server_client_ptr(const void *ptr)
{
  return (client_ptr_t)(ULONG_PTR)ptr;
}

/* convert a server client_ptr_t to a real pointer */
static inline void *wine_server_get_ptr(client_ptr_t ptr)
{
  return (void *)(ULONG_PTR)ptr;
}

#endif /* __WINE_NATIVE_SERVER_H */
