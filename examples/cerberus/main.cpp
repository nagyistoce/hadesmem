// Copyright (C) 2010-2015 Joshua Boyce
// See the file COPYING for copying permission.

#include "main.hpp"

#include <algorithm>
#include <functional>
#include <mutex>

#include <windows.h>

#include <hadesmem/config.hpp>
#include <hadesmem/detail/self_path.hpp>
#include <hadesmem/detail/region_alloc_size.hpp>
#include <hadesmem/detail/thread_aux.hpp>
#include <hadesmem/process.hpp>
#include <hadesmem/thread.hpp>
#include <hadesmem/thread_entry.hpp>
#include <hadesmem/thread_helpers.hpp>
#include <hadesmem/thread_list.hpp>

#include "ant_tweak_bar.hpp"
#include "cursor.hpp"
#include "d3d9.hpp"
#include "d3d10.hpp"
#include "d3d11.hpp"
#include "direct_input.hpp"
#include "dxgi.hpp"
#include "exception.hpp"
#include "gwen.hpp"
#include "helpers.hpp"
#include "input.hpp"
#include "module.hpp"
#include "opengl.hpp"
#include "plugin.hpp"
#include "process.hpp"
#include "render.hpp"
#include "window.hpp"

// WARNING! Most of this is untested, it's for expository and testing
// purposes only.

namespace
{
// This is a nasty hack to call any APIs which may be called from a static
// destructor. We want to ensure that we call it nice and early, so it's not
// called after we load our plugins, because otherwise it will be destructed
// before the plugin's are automatically unloaded via the static destructor of
// the plugin list, and when plugins try to unregister their callbacks (or
// whatever they're doing) they will go boom. This is a nasty workaround, but
// it's guaranteed by the standard to work, because we always use function local
// statics which are guaranteed to be destructed in a deterministic order.
void UseAllStatics()
{
  hadesmem::cerberus::GetThisProcess();

  auto& module = hadesmem::cerberus::GetModuleInterface();
  auto& d3d9 = hadesmem::cerberus::GetD3D9Interface();
  auto& dxgi = hadesmem::cerberus::GetDXGIInterface();
  auto& render = hadesmem::cerberus::GetRenderInterface();
  auto& ant_tweak_bar = hadesmem::cerberus::GetAntTweakBarInterface();
  auto& gwen = hadesmem::cerberus::GetGwenInterface();
  auto& window = hadesmem::cerberus::GetWindowInterface();
  auto& direct_input = hadesmem::cerberus::GetDirectInputInterface();
  auto& cursor = hadesmem::cerberus::GetCursorInterface();
  auto& input = hadesmem::cerberus::GetInputInterface();
  auto& exception = hadesmem::cerberus::GetExceptionInterface();
  auto& process = hadesmem::cerberus::GetProcessInterface();
  auto& helper = hadesmem::cerberus::GetHelperInterface();
  (void)helper;

  // Have to use 'real' callbacks rather than just passing in an empty
  // std::function object because we might not be the only thread running at the
  // moment and calling an empty function wrapper throws.

  auto const on_map_callback =
    [](HMODULE, std::wstring const&, std::wstring const&)
  {
  };
  auto const on_map_id = module.RegisterOnMap(on_map_callback);
  module.UnregisterOnMap(on_map_id);

  auto const on_unmap_callback = [](HMODULE)
  {
  };
  auto const on_unmap_id = module.RegisterOnUnmap(on_unmap_callback);
  module.UnregisterOnUnmap(on_unmap_id);

  auto const on_frame_callback_d3d9 = [](IDirect3DDevice9*)
  {
  };
  auto const on_frame_id_d3d9 = d3d9.RegisterOnFrame(on_frame_callback_d3d9);
  d3d9.UnregisterOnFrame(on_frame_id_d3d9);

  auto const on_reset_callback_d3d9 =
    [](IDirect3DDevice9*, D3DPRESENT_PARAMETERS*)
  {
  };
  auto const on_reset_id_d3d9 = d3d9.RegisterOnReset(on_reset_callback_d3d9);
  d3d9.UnregisterOnReset(on_reset_id_d3d9);

  auto const on_release_callback_d3d9 = [](IDirect3DDevice9*)
  {
  };
  auto const on_release_id_d3d9 =
    d3d9.RegisterOnRelease(on_release_callback_d3d9);
  d3d9.UnregisterOnRelease(on_release_id_d3d9);

  auto const on_set_stream_source_callback_d3d9 =
    [](IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT)
  {
  };
  auto const on_set_stream_source_id_d3d9 =
    d3d9.RegisterOnSetStreamSource(on_set_stream_source_callback_d3d9);
  d3d9.UnregisterOnSetStreamSource(on_set_stream_source_id_d3d9);

  auto const on_pre_dip_callback_d3d9 =
    [](IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT)
  {
  };
  auto const on_pre_dip_id_d3d9 =
    d3d9.RegisterOnPreDrawIndexedPrimitive(on_pre_dip_callback_d3d9);
  d3d9.UnregisterOnPreDrawIndexedPrimitive(on_pre_dip_id_d3d9);

  auto const on_post_dip_callback_d3d9 =
    [](IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT)
  {
  };
  auto const on_post_dip_id_d3d9 =
    d3d9.RegisterOnPostDrawIndexedPrimitive(on_post_dip_callback_d3d9);
  d3d9.UnregisterOnPostDrawIndexedPrimitive(on_post_dip_id_d3d9);

  auto const on_frame_callback_dxgi = [](IDXGISwapChain*)
  {
  };
  auto const on_frame_id_dxgi = dxgi.RegisterOnFrame(on_frame_callback_dxgi);
  dxgi.UnregisterOnFrame(on_frame_id_dxgi);

  auto const on_tw_init = [](hadesmem::cerberus::AntTweakBarInterface*)
  {
  };
  auto const on_tw_init_id = ant_tweak_bar.RegisterOnInitialize(on_tw_init);
  ant_tweak_bar.UnregisterOnInitialize(on_tw_init_id);

  auto const on_tw_cleanup = [](hadesmem::cerberus::AntTweakBarInterface*)
  {
  };
  auto const on_tw_cleanup_id = ant_tweak_bar.RegisterOnCleanup(on_tw_cleanup);
  ant_tweak_bar.UnregisterOnCleanup(on_tw_cleanup_id);

  auto const on_gwen_init = [](hadesmem::cerberus::GwenInterface*)
  {
  };
  auto const on_gwen_init_id = gwen.RegisterOnInitialize(on_gwen_init);
  gwen.UnregisterOnInitialize(on_gwen_init_id);

  auto const on_gwen_cleanup = [](hadesmem::cerberus::GwenInterface*)
  {
  };
  auto const on_gwen_cleanup_id = gwen.RegisterOnCleanup(on_gwen_cleanup);
  gwen.UnregisterOnCleanup(on_gwen_cleanup_id);

  auto const on_frame = [](hadesmem::cerberus::RenderApi, void*)
  {
  };
  auto const on_frame_id = render.RegisterOnFrame(on_frame);
  render.UnregisterOnFrame(on_frame_id);

  auto const on_set_gui_visibility = [](bool, bool)
  {
  };
  auto const on_set_gui_visibility_id =
    render.RegisterOnSetGuiVisibility(on_set_gui_visibility);
  render.UnregisterOnSetGuiVisibility(on_set_gui_visibility_id);

  auto const on_initialize_gui = [](hadesmem::cerberus::RenderApi, void*)
  {
  };
  auto const on_initialize_gui_id =
    render.RegisterOnInitializeGui(on_initialize_gui);
  render.UnregisterOnInitializeGui(on_initialize_gui_id);

  auto const on_cleanup_gui = [](hadesmem::cerberus::RenderApi)
  {
  };
  auto const on_cleanup_gui_id = render.RegisterOnCleanupGui(on_cleanup_gui);
  render.UnregisterOnCleanupGui(on_cleanup_gui_id);

  auto const on_wnd_proc_msg = [](HWND, UINT, WPARAM, LPARAM, bool*)
  {
  };
  auto const on_wnd_proc_msg_id = window.RegisterOnWndProcMsg(on_wnd_proc_msg);
  window.UnregisterOnWndProcMsg(on_wnd_proc_msg_id);

  auto const on_get_foreground_window = [](bool*, HWND*)
  {
  };
  auto const on_get_foreground_window_id =
    window.RegisterOnGetForegroundWindow(on_get_foreground_window);
  window.UnregisterOnGetForegroundWindow(on_get_foreground_window_id);

  auto const on_set_cursor = [](HCURSOR, bool*, HCURSOR*)
  {
  };
  auto const on_set_cursor_id = cursor.RegisterOnSetCursor(on_set_cursor);
  cursor.UnregisterOnSetCursor(on_set_cursor_id);

  auto const on_get_cursor_pos = [](LPPOINT, bool*)
  {
  };
  auto const on_get_cursor_pos_id =
    cursor.RegisterOnGetCursorPos(on_get_cursor_pos);
  cursor.UnregisterOnGetCursorPos(on_get_cursor_pos_id);

  auto const on_set_cursor_pos = [](int, int, bool*)
  {
  };
  auto const on_set_cursor_pos_id =
    cursor.RegisterOnSetCursorPos(on_set_cursor_pos);
  cursor.UnregisterOnSetCursorPos(on_set_cursor_pos_id);

  auto const on_show_cursor = [](BOOL, bool*, int*)
  {
  };
  auto const on_show_cursor_id = cursor.RegisterOnShowCursor(on_show_cursor);
  cursor.UnregisterOnShowCursor(on_show_cursor_id);

  auto const on_clip_cursor = [](RECT const*, bool*, BOOL*)
  {
  };
  auto const on_clip_cursor_id = cursor.RegisterOnClipCursor(on_clip_cursor);
  cursor.UnregisterOnClipCursor(on_clip_cursor_id);

  auto const on_get_clip_cursor = [](RECT*, bool*, BOOL*)
  {
  };
  auto const on_get_clip_cursor_id =
    cursor.RegisterOnGetClipCursor(on_get_clip_cursor);
  cursor.UnregisterOnGetClipCursor(on_get_clip_cursor_id);

  auto const on_direct_input = [](bool*)
  {
  };
  auto const on_direct_input_id =
    direct_input.RegisterOnDirectInput(on_direct_input);
  direct_input.UnregisterOnDirectInput(on_direct_input_id);

  auto const on_input_queue_entry = [](HWND, UINT, WPARAM, LPARAM)
  {
  };
  auto const on_input_queue_entry_id =
    input.RegisterOnInputQueueEntry(on_input_queue_entry);
  input.UnregisterOnInputQueueEntry(on_input_queue_entry_id);

  auto const on_rtl_add_vectored_exception_handler =
    [](ULONG, PVECTORED_EXCEPTION_HANDLER, bool*)
  {
  };
  auto const on_rtl_add_vectored_exception_handler_id =
    exception.RegisterOnRtlAddVectoredExceptionHandler(
      on_rtl_add_vectored_exception_handler);
  exception.UnregisterOnRtlAddVectoredExceptionHandler(
    on_rtl_add_vectored_exception_handler_id);

  auto const on_set_unhandled_exception_filter =
    [](LPTOP_LEVEL_EXCEPTION_FILTER, bool*)
  {
  };
  auto const on_set_unhandled_exception_filter_id =
    exception.RegisterOnSetUnhandledExceptionFilter(
      on_set_unhandled_exception_filter);
  exception.UnregisterOnSetUnhandledExceptionFilter(
    on_set_unhandled_exception_filter_id);

  auto const on_create_process_internal_w = [](HANDLE,
                                               LPCWSTR,
                                               LPWSTR,
                                               LPSECURITY_ATTRIBUTES,
                                               LPSECURITY_ATTRIBUTES,
                                               BOOL,
                                               DWORD,
                                               LPVOID,
                                               LPCWSTR,
                                               LPSTARTUPINFOW,
                                               LPPROCESS_INFORMATION,
                                               PHANDLE,
                                               bool*,
                                               BOOL*)
  {
  };
  auto const on_create_process_internal_w_id =
    process.RegisterOnCreateProcessInternalW(on_create_process_internal_w);
  process.UnregisterOnCreateProcessInternalW(on_create_process_internal_w_id);
}

// Check whether any threads are currently executing code in our module. This
// does not check whether we are on the stack, but that should be handled by the
// ref counting done in all the hooks. This is not foolproof, but it's better
// than nothing and will reduce the potential danger window even further.
bool IsSafeToUnload()
{
  auto const& process = hadesmem::cerberus::GetThisProcess();
  auto const this_module =
    reinterpret_cast<std::uint8_t*>(hadesmem::detail::GetHandleToSelf());
  auto const this_module_size = hadesmem::detail::GetRegionAllocSize(
    process, reinterpret_cast<void const*>(this_module));

  bool safe = false;
  for (std::size_t retries = 5; retries && !safe; --retries)
  {
    hadesmem::SuspendedProcess suspend{process.GetId()};
    hadesmem::ThreadList threads{process.GetId()};

    auto const is_unsafe = [&](hadesmem::ThreadEntry const& thread_entry)
    {
      auto const id = thread_entry.GetId();
      return id != ::GetCurrentThreadId() &&
             hadesmem::detail::IsExecutingInRange(
               thread_entry, this_module, this_module + this_module_size);
    };

    safe = std::find_if(std::begin(threads), std::end(threads), is_unsafe) ==
           std::end(threads);
  }

  return safe;
}

bool& GetInititalizeFlag()
{
  static bool initialized = false;
  return initialized;
}

std::mutex& GetInitializeMutex()
{
  static std::mutex mutex;
  return mutex;
}
}

namespace hadesmem
{
namespace cerberus
{
Process& GetThisProcess()
{
  static Process process{::GetCurrentProcessId()};
  return process;
}
}
}

extern "C" HADESMEM_DETAIL_DLLEXPORT DWORD_PTR Load() HADESMEM_DETAIL_NOEXCEPT
{
  try
  {
    std::mutex& mutex = GetInitializeMutex();
    std::lock_guard<std::mutex> lock(mutex);

    bool& is_initialized = GetInititalizeFlag();
    if (is_initialized)
    {
      HADESMEM_DETAIL_TRACE_A("Already initialized. Bailing.");
      return 1;
    }

    is_initialized = true;

    UseAllStatics();

    // Support deferred hooking (via module load notifications).
    hadesmem::cerberus::InitializeModule();
    hadesmem::cerberus::InitializeException();
    hadesmem::cerberus::InitializeProcess();
    hadesmem::cerberus::InitializeD3D9();
    hadesmem::cerberus::InitializeD3D10();
    hadesmem::cerberus::InitializeD3D101();
    hadesmem::cerberus::InitializeD3D11();
    hadesmem::cerberus::InitializeDXGI();
    hadesmem::cerberus::InitializeDirectInput();
    hadesmem::cerberus::InitializeCursor();
    hadesmem::cerberus::InitializeWindow();
    hadesmem::cerberus::InitializeRender();
    hadesmem::cerberus::InitializeInput();

    hadesmem::cerberus::DetourNtdllForModule(nullptr);
    hadesmem::cerberus::DetourNtdllForException(nullptr);
    hadesmem::cerberus::DetourKernelBaseForException(nullptr);
    hadesmem::cerberus::DetourKernelBaseForProcess(nullptr);
    hadesmem::cerberus::DetourD3D9(nullptr);
    hadesmem::cerberus::DetourD3D10(nullptr);
    hadesmem::cerberus::DetourD3D101(nullptr);
    hadesmem::cerberus::DetourD3D11(nullptr);
    hadesmem::cerberus::DetourDXGI(nullptr);
    hadesmem::cerberus::DetourDirectInput8(nullptr);
    hadesmem::cerberus::DetourUser32ForCursor(nullptr);
    hadesmem::cerberus::DetourUser32ForWindow(nullptr);
    hadesmem::cerberus::DetourOpenGL32(nullptr);

    hadesmem::cerberus::InitializeAntTweakBar();
    // hadesmem::cerberus::InitializeGwen();

    hadesmem::cerberus::LoadPlugins();

    return 0;
  }
  catch (...)
  {
    HADESMEM_DETAIL_TRACE_A(
      boost::current_exception_diagnostic_information().c_str());
    HADESMEM_DETAIL_ASSERT(false);

    return 1;
  }
}

extern "C" HADESMEM_DETAIL_DLLEXPORT DWORD_PTR Free() HADESMEM_DETAIL_NOEXCEPT
{
  try
  {
    std::mutex& mutex = GetInitializeMutex();
    std::lock_guard<std::mutex> lock(mutex);

    bool& is_initialized = GetInititalizeFlag();
    if (!is_initialized)
    {
      HADESMEM_DETAIL_TRACE_A("Already cleaned up. Bailing.");
      return 1;
    }

    is_initialized = false;

    hadesmem::cerberus::UndetourNtdllForModule(true);
    hadesmem::cerberus::UndetourNtdllForException(true);
    hadesmem::cerberus::UndetourKernelBaseForException(true);
    hadesmem::cerberus::UndetourKernelBaseForProcess(true);
    hadesmem::cerberus::UndetourDXGI(true);
    hadesmem::cerberus::UndetourD3D11(true);
    hadesmem::cerberus::UndetourD3D101(true);
    hadesmem::cerberus::UndetourD3D10(true);
    hadesmem::cerberus::UndetourD3D9(true);
    hadesmem::cerberus::UndetourDirectInput8(true);
    hadesmem::cerberus::UndetourUser32ForCursor(true);
    hadesmem::cerberus::UndetourUser32ForWindow(true);
    hadesmem::cerberus::UndetourOpenGL32(true);

    hadesmem::cerberus::UnloadPlugins();

    if (!IsSafeToUnload())
    {
      return 2;
    }

    return 0;
  }
  catch (...)
  {
    HADESMEM_DETAIL_TRACE_A(
      boost::current_exception_diagnostic_information().c_str());
    HADESMEM_DETAIL_ASSERT(false);

    return 1;
  }
}

BOOL WINAPI DllMain(HINSTANCE /*instance*/,
                    DWORD /*reason*/,
                    LPVOID /*reserved*/) HADESMEM_DETAIL_NOEXCEPT
{
  return TRUE;
}
