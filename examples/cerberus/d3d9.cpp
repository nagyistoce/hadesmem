// Copyright (C) 2010-2015 Joshua Boyce
// See the file COPYING for copying permission.

#include "d3d9.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>

#include <windows.h>
#include <winnt.h>
#include <winternl.h>

#include <d3d9.h>

#include <hadesmem/config.hpp>
#include <hadesmem/detail/last_error_preserver.hpp>
#include <hadesmem/detail/winternl.hpp>
#include <hadesmem/find_procedure.hpp>
#include <hadesmem/patcher.hpp>
#include <hadesmem/process.hpp>
#include <hadesmem/region.hpp>

#include "callbacks.hpp"
#include "direct_3d_9.hpp"
#include "helpers.hpp"
#include "main.hpp"
#include "module.hpp"

namespace
{
class D3D9Impl : public hadesmem::cerberus::D3D9Interface
{
public:
  virtual std::size_t RegisterOnFrame(
    std::function<hadesmem::cerberus::OnFrameD3D9Callback> const& callback)
    final
  {
    auto& callbacks = hadesmem::cerberus::GetOnFrameD3D9Callbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnFrame(std::size_t id) final
  {
    auto& callbacks = hadesmem::cerberus::GetOnFrameD3D9Callbacks();
    return callbacks.Unregister(id);
  }

  virtual std::size_t RegisterOnReset(
    std::function<hadesmem::cerberus::OnResetD3D9Callback> const& callback)
    final
  {
    auto& callbacks = hadesmem::cerberus::GetOnResetD3D9Callbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnReset(std::size_t id) final
  {
    auto& callbacks = hadesmem::cerberus::GetOnResetD3D9Callbacks();
    return callbacks.Unregister(id);
  }

  virtual std::size_t RegisterOnRelease(
    std::function<hadesmem::cerberus::OnReleaseD3D9Callback> const& callback)
    final
  {
    auto& callbacks = hadesmem::cerberus::GetOnReleaseD3D9Callbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnRelease(std::size_t id) final
  {
    auto& callbacks = hadesmem::cerberus::GetOnReleaseD3D9Callbacks();
    return callbacks.Unregister(id);
  }

  virtual std::size_t RegisterOnSetStreamSource(std::function<
    hadesmem::cerberus::OnSetStreamSourceD3D9Callback> const& callback) final
  {
    auto& callbacks = hadesmem::cerberus::GetOnSetStreamSourceD3D9Callbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnSetStreamSource(std::size_t id) final
  {
    auto& callbacks = hadesmem::cerberus::GetOnSetStreamSourceD3D9Callbacks();
    return callbacks.Unregister(id);
  }

  virtual std::size_t RegisterOnPreDrawIndexedPrimitive(std::function<
    hadesmem::cerberus::OnPreDrawIndexedPrimitiveD3D9Callback> const& callback)
    final
  {
    auto& callbacks =
      hadesmem::cerberus::GetOnPreDrawIndexedPrimitiveD3D9Callbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnPreDrawIndexedPrimitive(std::size_t id) final
  {
    auto& callbacks =
      hadesmem::cerberus::GetOnPreDrawIndexedPrimitiveD3D9Callbacks();
    return callbacks.Unregister(id);
  }

  virtual std::size_t RegisterOnPostDrawIndexedPrimitive(std::function<
    hadesmem::cerberus::OnPostDrawIndexedPrimitiveD3D9Callback> const& callback)
    final
  {
    auto& callbacks =
      hadesmem::cerberus::GetOnPostDrawIndexedPrimitiveD3D9Callbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnPostDrawIndexedPrimitive(std::size_t id) final
  {
    auto& callbacks =
      hadesmem::cerberus::GetOnPostDrawIndexedPrimitiveD3D9Callbacks();
    return callbacks.Unregister(id);
  }
};

std::unique_ptr<hadesmem::PatchDetour<decltype(&::Direct3DCreate9)>>&
  GetDirect3DCreate9Detour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<hadesmem::PatchDetour<decltype(&::Direct3DCreate9)>>
    detour;
  return detour;
}

std::unique_ptr<hadesmem::PatchDetour<decltype(&::Direct3DCreate9Ex)>>&
  GetDirect3DCreate9ExDetour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<hadesmem::PatchDetour<decltype(&::Direct3DCreate9Ex)>>
    detour;
  return detour;
}

std::pair<void*, SIZE_T>& GetD3D9Module() HADESMEM_DETAIL_NOEXCEPT
{
  static std::pair<void*, SIZE_T> module{};
  return module;
}

extern "C" IDirect3D9* WINAPI
  Direct3DCreate9Detour(hadesmem::PatchDetourBase* detour,
                        UINT sdk_version) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;

  HADESMEM_DETAIL_TRACE_FORMAT_A("Args: [%u].", sdk_version);

  auto const direct3d_create_9 =
    detour->GetTrampolineT<decltype(&Direct3DCreate9)>();
  last_error_preserver.Revert();
  auto ret = direct3d_create_9(sdk_version);
  last_error_preserver.Update();

  HADESMEM_DETAIL_TRACE_FORMAT_A("Ret: [%p].", ret);

  if (ret)
  {
    HADESMEM_DETAIL_TRACE_A("Succeeded.");
    HADESMEM_DETAIL_TRACE_A("Proxying IDirect3D9.");
    ret = new hadesmem::cerberus::Direct3D9Proxy(ret);
  }
  else
  {
    HADESMEM_DETAIL_TRACE_A("Failed.");
  }

  return ret;
}

extern "C" HRESULT WINAPI
  Direct3DCreate9ExDetour(hadesmem::PatchDetourBase* detour,
                          UINT sdk_version,
                          IDirect3D9Ex** d3d9_ex) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;

  HADESMEM_DETAIL_TRACE_FORMAT_A("Args: [%u] [%p].", sdk_version, d3d9_ex);
  auto const direct3d_create_9_ex =
    detour->GetTrampolineT<decltype(&Direct3DCreate9Ex)>();
  last_error_preserver.Revert();
  auto const ret = direct3d_create_9_ex(sdk_version, d3d9_ex);
  last_error_preserver.Update();
  HADESMEM_DETAIL_TRACE_FORMAT_A("Ret: [%p].", ret);

  if (SUCCEEDED(ret))
  {
    HADESMEM_DETAIL_TRACE_A("Succeeded.");
    HADESMEM_DETAIL_TRACE_A("Proxying IDirect3D9Ex.");
    *d3d9_ex = static_cast<IDirect3D9Ex*>(
      static_cast<void*>(new hadesmem::cerberus::Direct3D9Proxy(*d3d9_ex)));
  }
  else
  {
    HADESMEM_DETAIL_TRACE_A("Failed.");
  }

  return ret;
}
}

namespace hadesmem
{
namespace cerberus
{
Callbacks<OnFrameD3D9Callback>& GetOnFrameD3D9Callbacks()
{
  static Callbacks<OnFrameD3D9Callback> callbacks;
  return callbacks;
}

Callbacks<OnResetD3D9Callback>& GetOnResetD3D9Callbacks()
{
  static Callbacks<OnResetD3D9Callback> callbacks;
  return callbacks;
}

Callbacks<OnReleaseD3D9Callback>& GetOnReleaseD3D9Callbacks()
{
  static Callbacks<OnReleaseD3D9Callback> callbacks;
  return callbacks;
}

Callbacks<OnSetStreamSourceD3D9Callback>& GetOnSetStreamSourceD3D9Callbacks()
{
  static Callbacks<OnSetStreamSourceD3D9Callback> callbacks;
  return callbacks;
}

Callbacks<OnPreDrawIndexedPrimitiveD3D9Callback>&
  GetOnPreDrawIndexedPrimitiveD3D9Callbacks()
{
  static Callbacks<OnPreDrawIndexedPrimitiveD3D9Callback> callbacks;
  return callbacks;
}

Callbacks<OnPostDrawIndexedPrimitiveD3D9Callback>&
  GetOnPostDrawIndexedPrimitiveD3D9Callbacks()
{
  static Callbacks<OnPostDrawIndexedPrimitiveD3D9Callback> callbacks;
  return callbacks;
}

D3D9Interface& GetD3D9Interface() HADESMEM_DETAIL_NOEXCEPT
{
  static D3D9Impl d3d9_impl;
  return d3d9_impl;
}

void InitializeD3D9()
{
  auto& helper = GetHelperInterface();
  helper.InitializeSupportForModule(
    L"D3D9", DetourD3D9, UndetourD3D9, GetD3D9Module);
}

void DetourD3D9(HMODULE base)
{
  auto const& process = GetThisProcess();
  auto& module = GetD3D9Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonDetourModule(process, L"D3D9", base, module))
  {
    DetourFunc(process,
               base,
               "Direct3DCreate9",
               GetDirect3DCreate9Detour(),
               Direct3DCreate9Detour);
    DetourFunc(process,
               base,
               "Direct3DCreate9Ex",
               GetDirect3DCreate9ExDetour(),
               Direct3DCreate9ExDetour);
  }
}

void UndetourD3D9(bool remove)
{
  auto& module = GetD3D9Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonUndetourModule(L"D3D9", module))
  {
    UndetourFunc(L"Direct3DCreate9", GetDirect3DCreate9Detour(), remove);
    UndetourFunc(L"Direct3DCreate9Ex", GetDirect3DCreate9ExDetour(), remove);

    module = std::make_pair(nullptr, 0);
  }
}
}
}
