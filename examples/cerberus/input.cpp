// Copyright (C) 2010-2014 Joshua Boyce.
// See the file COPYING for copying permission.

#include "input.hpp"

#include <cstdint>

#include <windows.h>

#include <hadesmem/config.hpp>
#include <hadesmem/detail/detour_ref_counter.hpp>
#include <hadesmem/detail/last_error_preserver.hpp>
#include <hadesmem/patcher.hpp>

#include "callbacks.hpp"
#include "helpers.hpp"
#include "main.hpp"

namespace
{

struct WindowInfo
{
  HWND old_hwnd_{nullptr};
  WNDPROC old_wndproc_{nullptr};
  bool hooked_{false};
};

WindowInfo& GetWindowInfo() HADESMEM_DETAIL_NOEXCEPT
{
  static WindowInfo window_info;
  return window_info;
}

std::unique_ptr<hadesmem::PatchDetour>& GetDirectInput8CreateDetour()
  HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<hadesmem::PatchDetour> detour;
  return detour;
}

std::pair<void*, SIZE_T>& GetDirectInput8Module() HADESMEM_DETAIL_NOEXCEPT
{
  static std::pair<void*, SIZE_T> module{nullptr, 0};
  return module;
}

hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnWndProcMsgCallback>&
  GetOnWndProcMsgCallbacks()
{
  static hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnWndProcMsgCallback>
    callbacks;
  return callbacks;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  auto const& callbacks = GetOnWndProcMsgCallbacks();
  bool handled = false;
  callbacks.Run(hwnd, msg, wparam, lparam, handled);

  if (handled)
  {
    return 0;
  }

  WindowInfo& window_info = GetWindowInfo();
  return window_info.old_wndproc_
           ? ::CallWindowProcW(
               window_info.old_wndproc_, hwnd, msg, wparam, lparam)
           : ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

HRESULT WINAPI DirectInput8CreateDetour(HINSTANCE hinst,
                                        DWORD dwVersion,
                                        REFIID riidltf,
                                        LPVOID* ppvOut,
                                        LPUNKNOWN punkOuter)
{
  auto& detour = GetDirectInput8CreateDetour();
  hadesmem::detail::DetourRefCounter ref_count{detour->GetRefCount()};
  hadesmem::detail::LastErrorPreserver last_error_preserver;

  HADESMEM_DETAIL_TRACE_FORMAT_A("Args: [%p] [%u] [%p] [%p] [%p].",
                                 hinst,
                                 dwVersion,
                                 riidltf,
                                 ppvOut,
                                 punkOuter);
  auto const direct_input_8_create =
    detour->GetTrampoline<decltype(&DirectInput8CreateDetour)>();
  last_error_preserver.Revert();
  auto const ret =
    direct_input_8_create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
  last_error_preserver.Update();
  HADESMEM_DETAIL_TRACE_FORMAT_A("Ret: [%ld].", ret);

  if (FAILED(ret))
  {
    HADESMEM_DETAIL_TRACE_A("Failed.");
    return ret;
  }

  HADESMEM_DETAIL_TRACE_A("Succeeded.");

  return ret;
}

class InputImpl : public hadesmem::cerberus::InputInterface
{
public:
  ~InputImpl()
  {
    hadesmem::cerberus::HandleWindowChange(nullptr);
  }

  virtual std::size_t RegisterOnWndProcMsg(
    std::function<hadesmem::cerberus::OnWndProcMsgCallback> const& callback)
    final
  {
    return hadesmem::cerberus::RegisterOnWndProcMsgCallback(callback);
  }

  virtual void UnregisterOnWndProcMsg(std::size_t id) final
  {
    hadesmem::cerberus::UnregisterOnWndProcMsgCallback(id);
  }
};
}

namespace hadesmem
{

namespace cerberus
{

InputInterface& GetInputInterface() HADESMEM_DETAIL_NOEXCEPT
{
  static InputImpl input_impl;
  return input_impl;
}

void InitializeInput()
{
  InitializeSupportForModule(L"dinput8",
                             DetourDirectInput8,
                             UndetourDirectInput8,
                             GetDirectInput8Module);
}

void DetourDirectInput8(HMODULE base)
{
  auto const& process = GetThisProcess();
  auto& module = GetDirectInput8Module();
  if (CommonDetourModule(process, L"dinput8", base, module))
  {
    DetourFunc(process,
               base,
               "DirectInput8Create",
               GetDirectInput8CreateDetour(),
               DirectInput8CreateDetour);
  }
}

void UndetourDirectInput8(bool remove)
{
  auto& module = GetDirectInput8Module();
  if (CommonUndetourModule(L"dinput8", module))
  {
    UndetourFunc(L"DirectInput8Create", GetDirectInput8CreateDetour(), remove);

    module = std::make_pair(nullptr, 0);
  }
}

std::size_t RegisterOnWndProcMsgCallback(
  std::function<OnWndProcMsgCallback> const& callback)
{
  auto& callbacks = GetOnWndProcMsgCallbacks();
  return callbacks.Register(callback);
}

void UnregisterOnWndProcMsgCallback(std::size_t id)
{
  auto& callbacks = GetOnWndProcMsgCallbacks();
  return callbacks.Unregister(id);
}

void HandleWindowChange(HWND wnd)
{
  WindowInfo& window_info = GetWindowInfo();

  if (wnd == nullptr)
  {
    if (window_info.hooked_ && window_info.old_wndproc_ != nullptr)
    {
      ::SetWindowLongPtrW(window_info.old_hwnd_,
                          GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(window_info.old_wndproc_));
      HADESMEM_DETAIL_TRACE_FORMAT_A("Reset window procedure located at %p.",
                                     window_info.old_wndproc_);
    }

    window_info.old_hwnd_ = nullptr;
    window_info.old_wndproc_ = nullptr;
    window_info.hooked_ = false;

    return;
  }

  if (!window_info.hooked_)
  {
    window_info.old_hwnd_ = wnd;
    ::SetLastError(0);
    window_info.old_wndproc_ = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(
      wnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
    if (!window_info.old_wndproc_)
    {
      DWORD const last_error = ::GetLastError();
      if (last_error)
      {
        HADESMEM_DETAIL_THROW_EXCEPTION(
          Error{} << ErrorString{"SetWindowLongPtrW failed."}
                  << ErrorCodeWinLast{last_error});
      }
    }
    window_info.hooked_ = true;
    HADESMEM_DETAIL_TRACE_FORMAT_A("Replaced window procedure located at %p.",
                                   window_info.old_wndproc_);
  }
}

bool IsWindowHooked()
{
  return GetWindowInfo().hooked_;
}

HWND GetCurrentWindow()
{
  return GetWindowInfo().old_hwnd_;
}
}
}