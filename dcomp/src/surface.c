/*
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#include <stdarg.h>
#include <assert.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "dcomp_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

static HRESULT STDMETHODCALLTYPE surface_QueryInterface(IDCompositionSurfaceUnknown *iface, REFIID iid, void **out)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);

    TRACE("iface %p, iid %s, out %p!\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IDCompositionSurface)
            || IsEqualGUID(iid, &IID_IDCompositionSurfaceUnknown)
            || (surface->is_virtual && IsEqualGUID(iid, &IID_IDCompositionVirtualSurface)))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE surface_AddRef(IDCompositionSurfaceUnknown *iface)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    ULONG ref = InterlockedIncrement(&surface->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE surface_Release(IDCompositionSurfaceUnknown *iface)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    ULONG ref = InterlockedDecrement(&surface->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
    {
        IUnknown_Release(surface->physical_surface);
        IDCompositionSurfaceFactory_Release(surface->factory);
        if (surface->d2d_context)
            ID2D1DeviceContext_Release(surface->d2d_context);
        if (surface->draw_surface)
            ID3D11Texture2D_Release(surface->draw_surface);
        free(surface);
    }

    return ref;
}

/* Create a D2D device context rendering into the surface's draw texture. */
static HRESULT create_d2d_draw_context(struct composition_surface *surface,
        struct composition_surface_factory *factory, ID2D1DeviceContext **context)
{
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    ID2D1DeviceContext *device_context;
    IDXGISurface *dxgi_surface;
    ID2D1Device *d2d_device;
    ID2D1Bitmap1 *bitmap;
    HRESULT hr;

    if (IsEqualGUID(&factory->rendering_device_iid, &IID_ID2D1Device))
    {
        d2d_device = (ID2D1Device *)factory->rendering_device;
        ID2D1Device_AddRef(d2d_device);
    }
    else if (FAILED(hr = D2D1CreateDevice(factory->dxgi_device, NULL, &d2d_device)))
    {
        ERR("Failed to create a D2D device, hr %#lx.\n", hr);
        return hr;
    }

    hr = ID2D1Device_CreateDeviceContext(d2d_device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &device_context);
    ID2D1Device_Release(d2d_device);
    if (FAILED(hr))
    {
        ERR("Failed to create a D2D device context, hr %#lx.\n", hr);
        return hr;
    }

    if (FAILED(hr = ID3D11Texture2D_QueryInterface(surface->draw_surface, &IID_IDXGISurface, (void **)&dxgi_surface)))
    {
        ERR("Failed to get the draw IDXGISurface, hr %#lx.\n", hr);
        ID2D1DeviceContext_Release(device_context);
        return hr;
    }

    bitmap_desc.pixelFormat.format = surface->pixel_format;
    bitmap_desc.pixelFormat.alphaMode = surface->alpha_mode == DXGI_ALPHA_MODE_PREMULTIPLIED
            ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE;
    bitmap_desc.dpiX = 96.0f;
    bitmap_desc.dpiY = 96.0f;
    bitmap_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    bitmap_desc.colorContext = NULL;
    hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(device_context, dxgi_surface, &bitmap_desc, &bitmap);
    IDXGISurface_Release(dxgi_surface);
    if (FAILED(hr))
    {
        ERR("Failed to create a D2D target bitmap, hr %#lx.\n", hr);
        ID2D1DeviceContext_Release(device_context);
        return hr;
    }

    ID2D1DeviceContext_SetTarget(device_context, (ID2D1Image *)bitmap);
    ID2D1Bitmap1_Release(bitmap);

    *context = device_context;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_BeginDraw(IDCompositionSurfaceUnknown *iface,
        const RECT *rect, REFIID iid, void **object, POINT *offset)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(surface->factory);
    struct composition_device *device = impl_from_IDCompositionDevice(factory->device);
    D3D11_TEXTURE2D_DESC texture_desc;
    ID2D1DeviceContext *d2d_context;
    ID3D11Texture2D *draw_surface;
    ID3D11Device *d3d11_device;
    RECT whole_rect;
    HRESULT hr;

    TRACE("iface %p, rect %s, iid %s, object %p, offset %p.\n", iface, wine_dbgstr_rect(rect),
            debugstr_guid(iid), object, offset);

    if (!rect)
    {
        SetRect(&whole_rect, 0, 0, surface->width, surface->height);
        rect = &whole_rect;
    }

    /* The first BeginDraw must use the whole surface for non-virtual IDCompositionSurface */
    if (!surface->is_virtual && !surface->draw_surface && !(rect->left == 0 && rect->top == 0
            && rect->right == surface->width && rect->bottom == surface->height))
        return E_INVALIDARG;

    if (rect->left < 0 || rect->top < 0 || rect->right > surface->width
            || rect->bottom > surface->height || IsRectEmpty(rect))
        return E_INVALIDARG;

    if (!object || !offset)
        return E_INVALIDARG;

    if (!surface->draw_surface)
    {
        hr = IDXGIDevice_QueryInterface(factory->dxgi_device, &IID_ID3D11Device, (void **)&d3d11_device);
        if (FAILED(hr))
        {
            FIXME("Failed to get a d3d11 device, should a new one be created?\n");
            return hr;
        }

        texture_desc.Width = surface->width;
        texture_desc.Height = surface->height;
        texture_desc.MipLevels = 1;
        texture_desc.ArraySize = 1;
        texture_desc.Format = surface->pixel_format;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.SampleDesc.Quality = 0;
        texture_desc.Usage = D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        texture_desc.CPUAccessFlags = 0;
        texture_desc.MiscFlags = D3D11_RESOURCE_MISC_GUARDED;
        if (surface->pixel_format == DXGI_FORMAT_B8G8R8A8_UNORM)
            texture_desc.MiscFlags |= D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
        hr = ID3D11Device_CreateTexture2D(d3d11_device, &texture_desc, NULL, &draw_surface);
        ID3D11Device_Release(d3d11_device);
        if (FAILED(hr))
        {
            ERR("Failed to create a draw surface.\n");
            return hr;
        }

        if (InterlockedCompareExchangePointer((void **)&surface->draw_surface, draw_surface, 0))
            ID3D11Texture2D_Release(draw_surface);
    }

    /* On success the lock stays held until EndDraw, so that the composition thread,
     * which uses the same immediate context, can't run while the surface is drawn. */
    dcomp_lock();
    if (device->drawing_surface)
    {
        dcomp_unlock();
        return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    }
    device->drawing_surface = &surface->IDCompositionSurfaceUnknown_iface;

    if (IsEqualGUID(iid, &IID_ID2D1DeviceContext))
    {
        if (SUCCEEDED(hr = create_d2d_draw_context(surface, factory, &d2d_context)))
        {
            ID2D1DeviceContext_BeginDraw(d2d_context);
            surface->d2d_context = d2d_context;
            ID2D1DeviceContext_AddRef(d2d_context);
            *object = d2d_context;
        }
    }
    else
    {
        hr = IUnknown_QueryInterface(surface->draw_surface, iid, object);
    }
    if (FAILED(hr))
    {
        device->drawing_surface = NULL;
        dcomp_unlock();
        return hr;
    }

    offset->x = rect->left;
    offset->y = rect->top;
    surface->draw_rect = *rect;
    return S_OK;
}

/* Called with the dcomp lock held. */
static HRESULT end_draw(struct composition_surface *surface)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(surface->factory);
    ID3D11Resource *dst_resource, *src_resource;
    ID3D11DeviceContext *d3d11_device_context;
    ID3D11Device *d3d11_device;
    D3D11_BOX box;
    HRESULT hr;

    if (surface->d2d_context)
    {
        if (FAILED(hr = ID2D1DeviceContext_EndDraw(surface->d2d_context, NULL, NULL)))
            WARN("D2D EndDraw failed, hr %#lx.\n", hr);
        ID2D1DeviceContext_SetTarget(surface->d2d_context, NULL);
        ID2D1DeviceContext_Release(surface->d2d_context);
        surface->d2d_context = NULL;
    }

    /* TODO: Copy data after Commit() is called instead of doing it immediately here */

    hr = IDXGIDevice_QueryInterface(factory->dxgi_device, &IID_ID3D11Device, (void **)&d3d11_device);
    if (FAILED(hr))
    {
        FIXME("Failed to get a d3d11 device, should a new one be created?\n");
        return hr;
    }

    hr = ID3D11Texture2D_QueryInterface(surface->draw_surface, &IID_ID3D11Resource, (void **)&src_resource);
    if (FAILED(hr))
    {
        FIXME("Failed to get the source ID3D11Resource.\n");
        ID3D11Device_Release(d3d11_device);
        return hr;
    }

    hr = IUnknown_QueryInterface(surface->physical_surface, &IID_ID3D11Resource, (void **)&dst_resource);
    if (FAILED(hr))
    {
        FIXME("Failed to get the destination ID3D11Resource.\n");
        ID3D11Resource_Release(src_resource);
        ID3D11Device_Release(d3d11_device);
        return hr;
    }

    box.left = surface->draw_rect.left;
    box.top = surface->draw_rect.top;
    box.front = 0;
    box.right = surface->draw_rect.right;
    box.bottom = surface->draw_rect.bottom;
    box.back = 1;

    ID3D11Device_GetImmediateContext(d3d11_device, &d3d11_device_context);
    ID3D11DeviceContext_CopySubresourceRegion(d3d11_device_context, dst_resource, 0,
            surface->draw_rect.left, surface->draw_rect.top, 0, src_resource, 0, &box);
    ID3D11DeviceContext_Release(d3d11_device_context);
    ID3D11Resource_Release(dst_resource);
    ID3D11Resource_Release(src_resource);
    ID3D11Device_Release(d3d11_device);

    SetRectEmpty(&surface->draw_rect);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_EndDraw(IDCompositionSurfaceUnknown *iface)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(surface->factory);
    struct composition_device *device = impl_from_IDCompositionDevice(factory->device);
    HRESULT hr;

    TRACE("iface %p.\n", iface);

    dcomp_lock();
    if (!(device->drawing_surface && device->drawing_surface == &surface->IDCompositionSurfaceUnknown_iface))
    {
        dcomp_unlock();
        return DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    }
    device->drawing_surface = NULL;

    hr = end_draw(surface);

    dcomp_unlock();
    /* Release the lock held since BeginDraw. */
    dcomp_unlock();
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_SuspendDraw(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_ResumeDraw(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Scroll(IDCompositionSurfaceUnknown *iface, const RECT *scroll,
        const RECT *clip, int offset_x, int offset_y)
{
    FIXME("iface %p, scroll %s, clip %s, offset_x %d, offset_y %d stub!\n", iface,
            wine_dbgstr_rect(scroll), wine_dbgstr_rect(clip), offset_x, offset_y);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown1(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown2(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

/* Undocumented. Looks like a resize method. */
static HRESULT STDMETHODCALLTYPE surface_Unknown3(IDCompositionSurfaceUnknown *iface, UINT width, UINT height)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(surface->factory);
    struct composition_device *device = impl_from_IDCompositionDevice(factory->device);
    IDXGISurface *dxgi_surface;
    DXGI_SURFACE_DESC desc;
    HRESULT hr;

    FIXME("iface %p width %d height %d!\n", iface, width, height);

    if ((!width || !height) && !surface->is_virtual)
        return E_INVALIDARG;

    dcomp_lock();
    if (device->drawing_surface)
    {
        dcomp_unlock();
        return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    }

    if (width == surface->width && height == surface->height)
    {
        dcomp_unlock();
        return S_OK;
    }

    IUnknown_Release(surface->physical_surface);
    if (surface->draw_surface)
    {
        ID3D11Texture2D_Release(surface->draw_surface);
        surface->draw_surface = NULL;
    }

    desc.Width = max(width, 1);
    desc.Height = max(height, 1);
    desc.Format = surface->pixel_format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    hr = IDXGIDevice_CreateSurface(factory->dxgi_device, &desc, 1,
            DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_SHADER_INPUT, NULL, &dxgi_surface);
    if (FAILED(hr))
    {
        dcomp_unlock();
        ERR("Failed to create a IDXGISurface.\n");
        return hr;
    }

    surface->width = width;
    surface->height = height;
    surface->physical_surface = (IUnknown *)dxgi_surface;
    dcomp_unlock();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown4(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown5(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown6(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown7(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown8(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown9(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown10(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown11(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown12(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown13(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown14(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown15(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown16(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown17(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown18(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown19(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown20(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown21(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown22(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown23(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown24(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown25(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown26(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown27(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown28(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown29(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown30(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown31(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown32(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown33(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown34(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown35(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown36(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown37(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown38(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown39(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown40(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown41(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown42(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown43(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown44(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown45(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown46(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown47(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown48(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown49(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown50(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown51(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown52(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown53(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown54(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown55(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown56(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown57(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown58(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown59(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown60(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown61(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown62(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown63(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown64(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown65(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown66(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown67(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown68(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown69(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown70(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown71(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown72(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown73(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown74(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown75(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown76(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown77(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown78(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown79(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown80(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown81(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown82(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown83(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown84(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown85(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown86(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown87(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown88(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown89(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown90(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown91(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown92(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown93(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown94(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown95(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown96(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown97(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown98(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Unknown99(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static const struct IDCompositionSurfaceUnknownVtbl surface_vtbl =
{
    /* IUnknown methods */
    surface_QueryInterface,
    surface_AddRef,
    surface_Release,
    /* IDCompositionSurface methods */
    surface_BeginDraw,
    surface_EndDraw,
    surface_SuspendDraw,
    surface_ResumeDraw,
    surface_Scroll,
    /* IDCompositionSurfaceUnknown methods */
    surface_Unknown1,
    surface_Unknown2,
    surface_Unknown3,
    surface_Unknown4,
    surface_Unknown5,
    surface_Unknown6,
    surface_Unknown7,
    surface_Unknown8,
    surface_Unknown9,
    surface_Unknown10,
    surface_Unknown11,
    surface_Unknown12,
    surface_Unknown13,
    surface_Unknown14,
    surface_Unknown15,
    surface_Unknown16,
    surface_Unknown17,
    surface_Unknown18,
    surface_Unknown19,
    surface_Unknown20,
    surface_Unknown21,
    surface_Unknown22,
    surface_Unknown23,
    surface_Unknown24,
    surface_Unknown25,
    surface_Unknown26,
    surface_Unknown27,
    surface_Unknown28,
    surface_Unknown29,
    surface_Unknown30,
    surface_Unknown31,
    surface_Unknown32,
    surface_Unknown33,
    surface_Unknown34,
    surface_Unknown35,
    surface_Unknown36,
    surface_Unknown37,
    surface_Unknown38,
    surface_Unknown39,
    surface_Unknown40,
    surface_Unknown41,
    surface_Unknown42,
    surface_Unknown43,
    surface_Unknown44,
    surface_Unknown45,
    surface_Unknown46,
    surface_Unknown47,
    surface_Unknown48,
    surface_Unknown49,
    surface_Unknown50,
    surface_Unknown51,
    surface_Unknown52,
    surface_Unknown53,
    surface_Unknown54,
    surface_Unknown55,
    surface_Unknown56,
    surface_Unknown57,
    surface_Unknown58,
    surface_Unknown59,
    surface_Unknown60,
    surface_Unknown61,
    surface_Unknown62,
    surface_Unknown63,
    surface_Unknown64,
    surface_Unknown65,
    surface_Unknown66,
    surface_Unknown67,
    surface_Unknown68,
    surface_Unknown69,
    surface_Unknown70,
    surface_Unknown71,
    surface_Unknown72,
    surface_Unknown73,
    surface_Unknown74,
    surface_Unknown75,
    surface_Unknown76,
    surface_Unknown77,
    surface_Unknown78,
    surface_Unknown79,
    surface_Unknown80,
    surface_Unknown81,
    surface_Unknown82,
    surface_Unknown83,
    surface_Unknown84,
    surface_Unknown85,
    surface_Unknown86,
    surface_Unknown87,
    surface_Unknown88,
    surface_Unknown89,
    surface_Unknown90,
    surface_Unknown91,
    surface_Unknown92,
    surface_Unknown93,
    surface_Unknown94,
    surface_Unknown95,
    surface_Unknown96,
    surface_Unknown97,
    surface_Unknown98,
    surface_Unknown99,
};

/* IDCompositionVirtualSurface: same object as a regular surface, with Resize and
 * Trim in place of the undocumented methods following Scroll. */
static inline IDCompositionSurfaceUnknown *surface_from_IDCompositionVirtualSurface(IDCompositionVirtualSurface *iface)
{
    return (IDCompositionSurfaceUnknown *)iface;
}

static HRESULT STDMETHODCALLTYPE virtual_surface_QueryInterface(IDCompositionVirtualSurface *iface, REFIID iid, void **out)
{
    return surface_QueryInterface(surface_from_IDCompositionVirtualSurface(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE virtual_surface_AddRef(IDCompositionVirtualSurface *iface)
{
    return surface_AddRef(surface_from_IDCompositionVirtualSurface(iface));
}

static ULONG STDMETHODCALLTYPE virtual_surface_Release(IDCompositionVirtualSurface *iface)
{
    return surface_Release(surface_from_IDCompositionVirtualSurface(iface));
}

static HRESULT STDMETHODCALLTYPE virtual_surface_BeginDraw(IDCompositionVirtualSurface *iface,
        const RECT *rect, REFIID iid, void **object, POINT *offset)
{
    return surface_BeginDraw(surface_from_IDCompositionVirtualSurface(iface), rect, iid, object, offset);
}

static HRESULT STDMETHODCALLTYPE virtual_surface_EndDraw(IDCompositionVirtualSurface *iface)
{
    return surface_EndDraw(surface_from_IDCompositionVirtualSurface(iface));
}

static HRESULT STDMETHODCALLTYPE virtual_surface_SuspendDraw(IDCompositionVirtualSurface *iface)
{
    return surface_SuspendDraw(surface_from_IDCompositionVirtualSurface(iface));
}

static HRESULT STDMETHODCALLTYPE virtual_surface_ResumeDraw(IDCompositionVirtualSurface *iface)
{
    return surface_ResumeDraw(surface_from_IDCompositionVirtualSurface(iface));
}

static HRESULT STDMETHODCALLTYPE virtual_surface_Scroll(IDCompositionVirtualSurface *iface, const RECT *scroll,
        const RECT *clip, int offset_x, int offset_y)
{
    return surface_Scroll(surface_from_IDCompositionVirtualSurface(iface), scroll, clip, offset_x, offset_y);
}

static HRESULT STDMETHODCALLTYPE virtual_surface_Resize(IDCompositionVirtualSurface *iface, UINT width, UINT height)
{
    TRACE("iface %p, width %u, height %u.\n", iface, width, height);

    /* FIXME: Existing content is discarded instead of being preserved. */
    return surface_Unknown3(surface_from_IDCompositionVirtualSurface(iface), width, height);
}

static HRESULT STDMETHODCALLTYPE virtual_surface_Trim(IDCompositionVirtualSurface *iface, const RECT *rectangles, UINT count)
{
    TRACE("iface %p, rectangles %p, count %u.\n", iface, rectangles, count);

    /* Only a memory usage hint, nothing to do. */
    return S_OK;
}

static const struct IDCompositionVirtualSurfaceVtbl virtual_surface_vtbl =
{
    /* IUnknown methods */
    virtual_surface_QueryInterface,
    virtual_surface_AddRef,
    virtual_surface_Release,
    /* IDCompositionSurface methods */
    virtual_surface_BeginDraw,
    virtual_surface_EndDraw,
    virtual_surface_SuspendDraw,
    virtual_surface_ResumeDraw,
    virtual_surface_Scroll,
    /* IDCompositionVirtualSurface methods */
    virtual_surface_Resize,
    virtual_surface_Trim,
};

struct composition_surface *unsafe_impl_from_IDCompositionSurface(IDCompositionSurface *iface)
{
    if (!iface)
        return NULL;
    assert((void *)iface->lpVtbl == (void *)&surface_vtbl
            || (void *)iface->lpVtbl == (void *)&virtual_surface_vtbl);
    return CONTAINING_RECORD(iface, struct composition_surface, IDCompositionSurfaceUnknown_iface);
}

HRESULT create_surface(struct composition_surface_factory *factory, UINT width, UINT height,
        DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode, IDCompositionSurface **dcomp_surface)
{
    struct composition_surface *surface;
    const GUID *physical_surface_iid;
    IUnknown *physical_surface;
    HRESULT hr;

    if (pixel_format != DXGI_FORMAT_B8G8R8A8_UNORM && pixel_format != DXGI_FORMAT_R8G8B8A8_UNORM
            && pixel_format != DXGI_FORMAT_R16G16B16A16_FLOAT)
        return E_INVALIDARG;

    if (alpha_mode == DXGI_ALPHA_MODE_UNSPECIFIED)
        alpha_mode = DXGI_ALPHA_MODE_IGNORE;

    if (alpha_mode != DXGI_ALPHA_MODE_PREMULTIPLIED && alpha_mode != DXGI_ALPHA_MODE_IGNORE)
        return E_INVALIDARG;

    if (factory->dxgi_device)
    {
        IDXGISurface *dxgi_surface;
        DXGI_SURFACE_DESC desc;

        /* Virtual surfaces may be empty; DXGI surfaces can't, so back them with 1x1. */
        desc.Width = max(width, 1);
        desc.Height = max(height, 1);
        desc.Format = pixel_format;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        /* TODO: What about alpha_mode ? */

        hr = IDXGIDevice_CreateSurface(factory->dxgi_device, &desc, 1,
                DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_SHADER_INPUT, NULL, &dxgi_surface);
        if (FAILED(hr))
        {
            ERR("Failed to create a IDXGISurface.\n");
            return hr;
        }

        physical_surface = (IUnknown *)dxgi_surface;
        physical_surface_iid = &IID_IDXGISurface;
    }
    else
    {
        FIXME("No DXGI device available for rendering device guid %s.\n",
              wine_dbgstr_guid(&factory->rendering_device_iid));
        return E_NOTIMPL;
    }

    surface = calloc(1, sizeof(*surface));
    if (!surface)
        return E_OUTOFMEMORY;

    surface->IDCompositionSurfaceUnknown_iface.lpVtbl = &surface_vtbl;
    surface->factory = &factory->IDCompositionSurfaceFactory_iface;
    IDCompositionSurfaceFactory_AddRef(surface->factory);
    surface->physical_surface = physical_surface;
    surface->physical_surface_iid = *physical_surface_iid;
    surface->width = width;
    surface->height = height;
    surface->pixel_format = pixel_format;
    surface->alpha_mode = alpha_mode;
    surface->ref = 1;

    *dcomp_surface = (IDCompositionSurface *)&surface->IDCompositionSurfaceUnknown_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory_QueryInterface(IDCompositionSurfaceFactory *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p!\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IDCompositionSurfaceFactory))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_AddRef(IDCompositionSurfaceFactory *iface)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);
    ULONG ref = InterlockedIncrement(&factory->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE factory_Release(IDCompositionSurfaceFactory *iface)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);
    ULONG ref = InterlockedDecrement(&factory->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
    {
        IDCompositionDevice_Release(factory->device);
        IUnknown_Release(factory->rendering_device);
        if (factory->dxgi_device)
            IDXGIDevice_Release(factory->dxgi_device);
        free(factory);
    }

    return ref;
}

static HRESULT STDMETHODCALLTYPE factory_CreateSurface(IDCompositionSurfaceFactory *iface,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionSurface **surface)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);

    FIXME("iface %p, width %u, height %u, format %#x, alpha_mode %#x, surface %p semi-stub!\n", iface,
            width, height, pixel_format, alpha_mode, surface);

    if (!width || !height)
        return E_INVALIDARG;

    return create_surface(factory, width, height, pixel_format, alpha_mode, surface);
}

static HRESULT STDMETHODCALLTYPE factory_CreateVirtualSurface(IDCompositionSurfaceFactory *iface,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionVirtualSurface **surface)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);
    struct composition_surface *impl;
    IDCompositionSurface *dcomp_surface;
    HRESULT hr;

    FIXME("iface %p, width %u, height %u, format %#x, alpha_mode %#x, surface %p semi-stub!\n", iface,
            width, height, pixel_format, alpha_mode, surface);

    /* A virtual surface is backed by a full-size regular surface. */
    if (FAILED(hr = create_surface(factory, width, height, pixel_format, alpha_mode, &dcomp_surface)))
        return hr;

    impl = unsafe_impl_from_IDCompositionSurface(dcomp_surface);
    impl->IDCompositionSurfaceUnknown_iface.lpVtbl = (void *)&virtual_surface_vtbl;
    impl->is_virtual = TRUE;

    *surface = (IDCompositionVirtualSurface *)&impl->IDCompositionSurfaceUnknown_iface;
    return S_OK;
}

static const struct IDCompositionSurfaceFactoryVtbl factory_vtbl =
{
    /* IUnknown methods */
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IDCompositionSurfaceFactory methods */
    factory_CreateSurface,
    factory_CreateVirtualSurface,
};

/* ID2D1Device has no getter for its DXGI device; retrieve it through the
 * surface of a small target bitmap created on that device. */
static HRESULT get_dxgi_device_from_d2d_device(ID2D1Device *d2d_device, IDXGIDevice **dxgi_device)
{
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    ID2D1DeviceContext *device_context;
    IDXGISurface *dxgi_surface;
    ID2D1Bitmap1 *bitmap;
    D2D1_SIZE_U size = {1, 1};
    HRESULT hr;

    if (FAILED(hr = ID2D1Device_CreateDeviceContext(d2d_device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &device_context)))
        return hr;

    bitmap_desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmap_desc.dpiX = 96.0f;
    bitmap_desc.dpiY = 96.0f;
    bitmap_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    bitmap_desc.colorContext = NULL;
    hr = ID2D1DeviceContext_CreateBitmap(device_context, size, NULL, 0, &bitmap_desc, &bitmap);
    ID2D1DeviceContext_Release(device_context);
    if (FAILED(hr))
        return hr;

    hr = ID2D1Bitmap1_GetSurface(bitmap, &dxgi_surface);
    ID2D1Bitmap1_Release(bitmap);
    if (FAILED(hr))
        return hr;

    hr = IDXGISurface_GetDevice(dxgi_surface, &IID_IDXGIDevice, (void **)dxgi_device);
    IDXGISurface_Release(dxgi_surface);
    return hr;
}

HRESULT create_surface_factory(struct composition_device *device, IUnknown *rendering_device,
        IDCompositionSurfaceFactory **obj)
{
    struct composition_surface_factory *factory;
    IDXGIDevice *dxgi_device = NULL;
    ID2D1Device *d2d_device;
    const GUID *iid;
    HRESULT hr;

    if (!rendering_device)
        return E_INVALIDARG;

    if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_IDXGIDevice, (void *)&dxgi_device)))
    {
        TRACE("Creating a surface factory with an IDXGIDevice rendering device.\n");
        iid = &IID_IDXGIDevice;
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_ID2D1Device, (void *)&d2d_device)))
    {
        TRACE("Creating a surface factory with an IID_ID2D1Device rendering device.\n");
        iid = &IID_ID2D1Device;
        if (FAILED(hr = get_dxgi_device_from_d2d_device(d2d_device, &dxgi_device)))
        {
            ERR("Failed to get the DXGI device of the D2D device, hr %#lx.\n", hr);
            dxgi_device = NULL;
        }
    }
    else
    {
        ERR("Unknown rendering device.\n");
        return E_NOINTERFACE;
    }

    factory = calloc(1, sizeof(*factory));
    if (!factory)
        return E_OUTOFMEMORY;

    factory->IDCompositionSurfaceFactory_iface.lpVtbl = &factory_vtbl;
    factory->ref = 1;
    factory->rendering_device = IsEqualGUID(iid, &IID_IDXGIDevice) ? (IUnknown *)dxgi_device : (IUnknown *)d2d_device;
    factory->rendering_device_iid = *iid;
    factory->dxgi_device = dxgi_device;
    if (dxgi_device && IsEqualGUID(iid, &IID_IDXGIDevice))
        IDXGIDevice_AddRef(dxgi_device);
    factory->device = &device->IDCompositionDevice_iface;
    IDCompositionDevice_AddRef(&device->IDCompositionDevice_iface);

    *obj = &factory->IDCompositionSurfaceFactory_iface;
    return S_OK;
}
