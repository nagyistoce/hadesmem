// Copyright (C) 2010-2015 Joshua Boyce
// See the file COPYING for copying permission.

#include "d3d10.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>

#include <windows.h>
#include <winnt.h>
#include <winternl.h>

#include <d3d10_1.h>
#include <d3d10.h>

#include <hadesmem/config.hpp>
#include <hadesmem/detail/last_error_preserver.hpp>
#include <hadesmem/detail/winternl.hpp>
#include <hadesmem/find_procedure.hpp>
#include <hadesmem/patcher.hpp>
#include <hadesmem/process.hpp>
#include <hadesmem/region.hpp>

#include "callbacks.hpp"
#include "d3d10_device.hpp"
#include "dxgi.hpp"
#include "dxgi_swap_chain.hpp"
#include "helpers.hpp"
#include "hook_counter.hpp"
#include "main.hpp"
#include "module.hpp"

namespace
{
class D3D10Impl : public hadesmem::cerberus::D3D10Interface
{
public:
  virtual std::size_t RegisterOnRelease(
    std::function<hadesmem::cerberus::OnReleaseD3D10Callback> const& callback)
    final
  {
    auto& callbacks = hadesmem::cerberus::GetOnReleaseD3D10Callbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnRelease(std::size_t id) final
  {
    auto& callbacks = hadesmem::cerberus::GetOnReleaseD3D10Callbacks();
    return callbacks.Unregister(id);
  }
};

std::unique_ptr<hadesmem::PatchDetour<decltype(&::D3D10CreateDevice)>>&
  GetD3D10CreateDeviceDetour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<hadesmem::PatchDetour<decltype(&::D3D10CreateDevice)>>
    detour;
  return detour;
}

std::unique_ptr<
  hadesmem::PatchDetour<decltype(&::D3D10CreateDeviceAndSwapChain)>>&
  GetD3D10CreateDeviceAndSwapChainDetour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<
    hadesmem::PatchDetour<decltype(&::D3D10CreateDeviceAndSwapChain)>> detour;
  return detour;
}

std::unique_ptr<hadesmem::PatchDetour<decltype(&::D3D10CreateDevice1)>>&
  GetD3D10CreateDevice1Detour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<hadesmem::PatchDetour<decltype(&::D3D10CreateDevice1)>>
    detour;
  return detour;
}

std::unique_ptr<
  hadesmem::PatchDetour<decltype(&::D3D10CreateDeviceAndSwapChain1)>>&
  GetD3D10CreateDeviceAndSwapChain1Detour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<
    hadesmem::PatchDetour<decltype(&::D3D10CreateDeviceAndSwapChain1)>> detour;
  return detour;
}

std::pair<void*, SIZE_T>& GetD3D10Module() HADESMEM_DETAIL_NOEXCEPT
{
  static std::pair<void*, SIZE_T> module{};
  return module;
}

std::pair<void*, SIZE_T>& GetD3D101Module() HADESMEM_DETAIL_NOEXCEPT
{
  static std::pair<void*, SIZE_T> module{};
  return module;
}

std::uint32_t& GetD3D10CreateHookCount() HADESMEM_DETAIL_NOEXCEPT
{
  static __declspec(thread) std::uint32_t in_hook = 0;
  return in_hook;
}

extern "C" HRESULT WINAPI
  D3D10CreateDeviceDetour(hadesmem::PatchDetourBase* detour,
                          IDXGIAdapter* adapter,
                          D3D10_DRIVER_TYPE driver_type,
                          HMODULE software,
                          UINT flags,
                          UINT sdk_version,
                          ID3D10Device** device) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;
  hadesmem::cerberus::HookCounter hook_counter{&GetD3D10CreateHookCount()};

  HADESMEM_DETAIL_TRACE_FORMAT_A("Args: [%p] [%d] [%p] [%u] [%u] [%p].",
                                 adapter,
                                 driver_type,
                                 software,
                                 flags,
                                 sdk_version,
                                 device);
  auto const d3d10_create_device =
    detour->GetTrampolineT<decltype(&D3D10CreateDevice)>();
  last_error_preserver.Revert();
  auto const ret = d3d10_create_device(
    adapter, driver_type, software, flags, sdk_version, device);
  last_error_preserver.Update();
  HADESMEM_DETAIL_TRACE_FORMAT_A("Ret: [%ld].", ret);

  if (FAILED(ret))
  {
    HADESMEM_DETAIL_TRACE_A("Failed.");
    return ret;
  }

  HADESMEM_DETAIL_TRACE_A("Succeeded.");

  if (!device)
  {
    HADESMEM_DETAIL_TRACE_A("Invalid device out param pointer.");
    return ret;
  }

  auto const hook_count = hook_counter.GetCount();
  HADESMEM_DETAIL_ASSERT(hook_count > 0);
  if (hook_count == 1)
  {
    HADESMEM_DETAIL_TRACE_A("Proxying ID3D10Device.");
    *device = new hadesmem::cerberus::D3D10DeviceProxy{*device};
  }

  return ret;
}

extern "C" HRESULT WINAPI D3D10CreateDeviceAndSwapChainDetour(
  hadesmem::PatchDetourBase* detour,
  IDXGIAdapter* adapter,
  D3D10_DRIVER_TYPE driver_type,
  HMODULE software,
  UINT flags,
  UINT sdk_version,
  DXGI_SWAP_CHAIN_DESC* swap_chain_desc,
  IDXGISwapChain** swap_chain,
  ID3D10Device** device) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;
  hadesmem::cerberus::HookCounter hook_counter{&GetD3D10CreateHookCount()};

  HADESMEM_DETAIL_TRACE_FORMAT_A(
    "Args: [%p] [%d] [%p] [%u] [%u] [%p] [%p] [%p].",
    adapter,
    driver_type,
    software,
    flags,
    sdk_version,
    swap_chain_desc,
    swap_chain,
    device);
  auto const d3d10_create_device_and_swap_chain =
    detour->GetTrampolineT<decltype(&D3D10CreateDeviceAndSwapChain)>();
  last_error_preserver.Revert();
  auto const ret = d3d10_create_device_and_swap_chain(adapter,
                                                      driver_type,
                                                      software,
                                                      flags,
                                                      sdk_version,
                                                      swap_chain_desc,
                                                      swap_chain,
                                                      device);
  last_error_preserver.Update();
  HADESMEM_DETAIL_TRACE_FORMAT_A("Ret: [%ld].", ret);

  if (SUCCEEDED(ret))
  {
    HADESMEM_DETAIL_TRACE_A("Succeeded.");

    auto const hook_count = hook_counter.GetCount();
    HADESMEM_DETAIL_ASSERT(hook_count > 0);

    if (swap_chain)
    {
      if (hook_count == 1)
      {
        HADESMEM_DETAIL_TRACE_A("Proxying IDXGISwapChain.");
        *swap_chain = new hadesmem::cerberus::DXGISwapChainProxy{*swap_chain};
      }
    }
    else
    {
      HADESMEM_DETAIL_TRACE_A("Invalid swap chain out param pointer.");
    }

    if (device)
    {
      if (hook_count == 1)
      {
        HADESMEM_DETAIL_TRACE_A("Proxying ID3D10Device.");
        *device = new hadesmem::cerberus::D3D10DeviceProxy{*device};
      }
    }
    else
    {
      HADESMEM_DETAIL_TRACE_A("Invalid device out param pointer.");
    }
  }
  else
  {
    HADESMEM_DETAIL_TRACE_A("Failed.");
  }

  return ret;
}

extern "C" HRESULT WINAPI
  D3D10CreateDevice1Detour(hadesmem::PatchDetourBase* detour,
                           IDXGIAdapter* adapter,
                           D3D10_DRIVER_TYPE driver_type,
                           HMODULE software,
                           UINT flags,
                           D3D10_FEATURE_LEVEL1 hardware_level,
                           UINT sdk_version,
                           ID3D10Device1** device) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;
  hadesmem::cerberus::HookCounter hook_counter{&GetD3D10CreateHookCount()};

  HADESMEM_DETAIL_TRACE_FORMAT_A("Args: [%p] [%d] [%p] [%u] [%d] [%u] [%p].",
                                 adapter,
                                 driver_type,
                                 software,
                                 flags,
                                 hardware_level,
                                 sdk_version,
                                 device);
  auto const d3d10_create_device_1 =
    detour->GetTrampolineT<decltype(&D3D10CreateDevice1)>();
  last_error_preserver.Revert();
  auto const ret = d3d10_create_device_1(
    adapter, driver_type, software, flags, hardware_level, sdk_version, device);
  last_error_preserver.Update();
  HADESMEM_DETAIL_TRACE_FORMAT_A("Ret: [%ld].", ret);

  if (FAILED(ret))
  {
    HADESMEM_DETAIL_TRACE_A("Failed.");
    return ret;
  }

  HADESMEM_DETAIL_TRACE_A("Succeeded.");

  if (!device)
  {
    HADESMEM_DETAIL_TRACE_A("Invalid device out param pointer.");
    return ret;
  }

  auto const hook_count = hook_counter.GetCount();
  HADESMEM_DETAIL_ASSERT(hook_count > 0);
  if (hook_count == 1)
  {
    HADESMEM_DETAIL_TRACE_A("Proxying ID3D10Device.");
    *device = new hadesmem::cerberus::D3D10DeviceProxy{*device};
  }

  return ret;
}

extern "C" HRESULT WINAPI D3D10CreateDeviceAndSwapChain1Detour(
  hadesmem::PatchDetourBase* detour,
  IDXGIAdapter* adapter,
  D3D10_DRIVER_TYPE driver_type,
  HMODULE software,
  UINT flags,
  D3D10_FEATURE_LEVEL1 hardware_level,
  UINT sdk_version,
  DXGI_SWAP_CHAIN_DESC* swap_chain_desc,
  IDXGISwapChain** swap_chain,
  ID3D10Device1** device) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;
  hadesmem::cerberus::HookCounter hook_counter{&GetD3D10CreateHookCount()};

  HADESMEM_DETAIL_TRACE_FORMAT_A(
    "Args: [%p] [%d] [%p] [%u] [%d] [%u] [%p] [%p] [%p].",
    adapter,
    driver_type,
    software,
    flags,
    hardware_level,
    sdk_version,
    swap_chain_desc,
    swap_chain,
    device);
  auto const d3d10_create_device_and_swap_chain_1 =
    detour->GetTrampolineT<decltype(&D3D10CreateDeviceAndSwapChain1)>();
  last_error_preserver.Revert();
  auto const ret = d3d10_create_device_and_swap_chain_1(adapter,
                                                        driver_type,
                                                        software,
                                                        flags,
                                                        hardware_level,
                                                        sdk_version,
                                                        swap_chain_desc,
                                                        swap_chain,
                                                        device);
  last_error_preserver.Update();
  HADESMEM_DETAIL_TRACE_FORMAT_A("Ret: [%ld].", ret);

  if (SUCCEEDED(ret))
  {
    HADESMEM_DETAIL_TRACE_A("Succeeded.");

    auto const hook_count = hook_counter.GetCount();
    HADESMEM_DETAIL_ASSERT(hook_count > 0);

    if (swap_chain)
    {
      if (hook_count == 1)
      {
        HADESMEM_DETAIL_TRACE_A("Proxying IDXGISwapChain.");
        *swap_chain = new hadesmem::cerberus::DXGISwapChainProxy{*swap_chain};
      }
    }
    else
    {
      HADESMEM_DETAIL_TRACE_A("Invalid swap chain out param pointer.");
    }

    if (device)
    {
      if (hook_count == 1)
      {
        HADESMEM_DETAIL_TRACE_A("Proxying ID3D10Device.");
        *device = new hadesmem::cerberus::D3D10DeviceProxy{*device};
      }
    }
    else
    {
      HADESMEM_DETAIL_TRACE_A("Invalid device out param pointer.");
    }
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
Callbacks<OnReleaseD3D10Callback>& GetOnReleaseD3D10Callbacks()
{
  static Callbacks<OnReleaseD3D10Callback> callbacks;
  return callbacks;
}

D3D10Interface& GetD3D10Interface() HADESMEM_DETAIL_NOEXCEPT
{
  static D3D10Impl d3d10_impl;
  return d3d10_impl;
}

void InitializeD3D10()
{
  auto& helper = GetHelperInterface();
  helper.InitializeSupportForModule(
    L"D3D10", DetourD3D10, UndetourD3D10, GetD3D10Module);
}

void InitializeD3D101()
{
  auto& helper = GetHelperInterface();
  helper.InitializeSupportForModule(
    L"D3D10_1", DetourD3D101, UndetourD3D101, GetD3D101Module);
}

void DetourD3D10(HMODULE base)
{
  auto const& process = GetThisProcess();
  auto& module = GetD3D10Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonDetourModule(process, L"D3D10", base, module))
  {
    DetourFunc(process,
               base,
               "D3D10CreateDevice",
               GetD3D10CreateDeviceDetour(),
               D3D10CreateDeviceDetour);
    DetourFunc(process,
               base,
               "D3D10CreateDeviceAndSwapChain",
               GetD3D10CreateDeviceAndSwapChainDetour(),
               D3D10CreateDeviceAndSwapChainDetour);
  }
}

void UndetourD3D10(bool remove)
{
  auto& module = GetD3D10Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonUndetourModule(L"D3D10", module))
  {
    UndetourFunc(L"D3D10CreateDeviceAndSwapChain",
                 GetD3D10CreateDeviceAndSwapChainDetour(),
                 remove);
    UndetourFunc(L"D3D10CreateDevice", GetD3D10CreateDeviceDetour(), remove);

    module = std::make_pair(nullptr, 0);
  }
}

void DetourD3D101(HMODULE base)
{
  auto const& process = GetThisProcess();
  auto& module = GetD3D101Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonDetourModule(process, L"D3D10_1", base, module))
  {
    DetourFunc(process,
               base,
               "D3D10CreateDevice1",
               GetD3D10CreateDevice1Detour(),
               D3D10CreateDevice1Detour);
    DetourFunc(process,
               base,
               "D3D10CreateDeviceAndSwapChain1",
               GetD3D10CreateDeviceAndSwapChain1Detour(),
               D3D10CreateDeviceAndSwapChain1Detour);
  }
}

void UndetourD3D101(bool remove)
{
  auto& module = GetD3D10Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonUndetourModule(L"D3D10_1", module))
  {
    UndetourFunc(L"D3D10CreateDeviceAndSwapChain1",
                 GetD3D10CreateDeviceAndSwapChain1Detour(),
                 remove);
    UndetourFunc(L"D3D10CreateDevice1", GetD3D10CreateDevice1Detour(), remove);

    module = std::make_pair(nullptr, 0);
  }
}
}
}
