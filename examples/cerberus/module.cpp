// Copyright (C) 2010-2015 Joshua Boyce
// See the file COPYING for copying permission.

#include "module.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>

#include <windows.h>
#include <winnt.h>
#include <winternl.h>

#include <hadesmem/config.hpp>
#include <hadesmem/detail/last_error_preserver.hpp>
#include <hadesmem/detail/recursion_protector.hpp>
#include <hadesmem/detail/winternl.hpp>
#include <hadesmem/find_procedure.hpp>
#include <hadesmem/module.hpp>
#include <hadesmem/patcher.hpp>
#include <hadesmem/process.hpp>

#include "callbacks.hpp"
#include "helpers.hpp"
#include "main.hpp"

namespace winternl = hadesmem::detail::winternl;

namespace
{
std::pair<void*, SIZE_T>& GetNtdllModule() HADESMEM_DETAIL_NOEXCEPT
{
  static std::pair<void*, SIZE_T> module{nullptr, 0};
  return module;
}

hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnMapCallback>&
  GetOnMapCallbacks()
{
  static hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnMapCallback>
    callbacks;
  return callbacks;
}

hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnUnmapCallback>&
  GetOnUnmapCallbacks()
{
  static hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnUnmapCallback>
    callbacks;
  return callbacks;
}

class ModuleImpl : public hadesmem::cerberus::ModuleInterface
{
public:
  virtual std::size_t RegisterOnMap(
    std::function<hadesmem::cerberus::OnMapCallback> const& callback) final
  {
    auto& callbacks = GetOnMapCallbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnMap(std::size_t id) final
  {
    auto& callbacks = GetOnMapCallbacks();
    return callbacks.Unregister(id);
  }

  virtual std::size_t RegisterOnUnmap(
    std::function<hadesmem::cerberus::OnUnmapCallback> const& callback) final
  {
    auto& callbacks = GetOnUnmapCallbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnUnmap(std::size_t id) final
  {
    auto& callbacks = GetOnUnmapCallbacks();
    return callbacks.Unregister(id);
  }
};

extern "C" NTSTATUS WINAPI
  NtMapViewOfSection(HANDLE section,
                     HANDLE process,
                     PVOID* base,
                     ULONG_PTR zero_bits,
                     SIZE_T commit_size,
                     PLARGE_INTEGER section_offset,
                     PSIZE_T view_size,
                     winternl::SECTION_INHERIT inherit_disposition,
                     ULONG alloc_type,
                     ULONG alloc_protect);

extern "C" NTSTATUS WINAPI NtUnmapViewOfSection(HANDLE process, PVOID base);

std::unique_ptr<hadesmem::PatchDetour<decltype(&NtMapViewOfSection)>>&
  GetNtMapViewOfSectionDetour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<hadesmem::PatchDetour<decltype(&NtMapViewOfSection)>>
    detour;
  return detour;
}

std::unique_ptr<hadesmem::PatchDetour<decltype(&NtUnmapViewOfSection)>>&
  GetNtUnmapViewOfSectionDetour() HADESMEM_DETAIL_NOEXCEPT
{
  static std::unique_ptr<hadesmem::PatchDetour<decltype(&NtUnmapViewOfSection)>>
    detour;
  return detour;
}

extern "C" NTSTATUS WINAPI
  NtMapViewOfSectionDetour(hadesmem::PatchDetourBase* detour,
                           HANDLE section,
                           HANDLE process,
                           PVOID* base,
                           ULONG_PTR zero_bits,
                           SIZE_T commit_size,
                           PLARGE_INTEGER section_offset,
                           PSIZE_T view_size,
                           winternl::SECTION_INHERIT inherit_disposition,
                           ULONG alloc_type,
                           ULONG alloc_protect) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;

  auto const nt_map_view_of_section =
    detour->GetTrampolineT<decltype(&NtMapViewOfSection)>();
  last_error_preserver.Revert();
  auto const ret = nt_map_view_of_section(section,
                                          process,
                                          base,
                                          zero_bits,
                                          commit_size,
                                          section_offset,
                                          view_size,
                                          inherit_disposition,
                                          alloc_type,
                                          alloc_protect);
  last_error_preserver.Update();

  static __declspec(thread) std::int32_t in_hook = 0;
  if (in_hook)
  {
    return ret;
  }

  // Need recursion protection because NtMapViewOfSection is eventually called
  // by a lot of APIs, and we can't really avoid them all.
  hadesmem::detail::RecursionProtector recursion_protector{&in_hook};

  // This has to be after all our recursion checks, rather than before (which
  // would be better) because OutputDebugString calls MapViewOfFile when DBWIN
  // is running.
  HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A(
    "Args: [%p] [%p] [%p] [%Iu] [%Iu] [%p] [%p] [%d] [%u] [%u].",
    section,
    process,
    base,
    zero_bits,
    commit_size,
    section_offset,
    view_size,
    inherit_disposition,
    alloc_type,
    alloc_protect);
  HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A("Ret: [%ld].", ret);

  if (!NT_SUCCESS(ret))
  {
    HADESMEM_DETAIL_TRACE_NOISY_A("Failed.");
  }

  DWORD const pid = ::GetProcessId(process);
  if (!pid || pid != ::GetCurrentProcessId())
  {
    HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A("Unkown or different process [%lu].",
                                         pid);
    return ret;
  }

  HADESMEM_DETAIL_TRACE_NOISY_A("Current process.");

  try
  {
    HADESMEM_DETAIL_TRACE_NOISY_A("Succeeded.");

    hadesmem::Region const region{hadesmem::cerberus::GetThisProcess(), *base};
    DWORD const region_type = region.GetType();
    if (region_type != MEM_IMAGE)
    {
      HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A("Not an image. Type given was %lx.",
                                           region_type);
      return ret;
    }

    void* const arbitrary_user_pointer =
      winternl::GetCurrentTeb()->NtTib.ArbitraryUserPointer;
    if (!arbitrary_user_pointer)
    {
      HADESMEM_DETAIL_TRACE_NOISY_A("No arbitrary user pointer.");
      return ret;
    }

    std::wstring const path{static_cast<PCWSTR>(arbitrary_user_pointer)};
    HADESMEM_DETAIL_TRACE_FORMAT_W(L"Path is %s.", path.c_str());

    auto const backslash = path.find_last_of(L'\\');
    std::size_t const name_beg =
      (backslash != std::wstring::npos ? backslash + 1 : 0);
    std::wstring const module_name(std::begin(path) + name_beg, std::end(path));
    HADESMEM_DETAIL_TRACE_FORMAT_W(L"Module name is %s.", module_name.c_str());
    std::wstring const module_name_upper =
      hadesmem::detail::ToUpperOrdinal(module_name);

    auto& callbacks = GetOnMapCallbacks();
    callbacks.Run(reinterpret_cast<HMODULE>(*base), path, module_name_upper);
  }
  catch (...)
  {
    HADESMEM_DETAIL_TRACE_A(
      boost::current_exception_diagnostic_information().c_str());
    HADESMEM_DETAIL_ASSERT(false);
  }

  return ret;
}

extern "C" NTSTATUS WINAPI
  NtUnmapViewOfSectionDetour(hadesmem::PatchDetourBase* detour,
                             HANDLE process,
                             PVOID base) HADESMEM_DETAIL_NOEXCEPT
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;

  auto const nt_unmap_view_of_section =
    detour->GetTrampolineT<decltype(&NtUnmapViewOfSection)>();
  last_error_preserver.Revert();
  auto const ret = nt_unmap_view_of_section(process, base);
  last_error_preserver.Update();

  static __declspec(thread) std::int32_t in_hook = 0;
  if (in_hook)
  {
    return ret;
  }

  hadesmem::detail::RecursionProtector recursion_protector{&in_hook};

  // This has to be after all our recursion checks, rather than before (which
  // would be better) because OutputDebugString calls UnmapViewOfFile when DBWIN
  // is running.
  HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A("Args: [%p] [%p].", process, base);
  HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A("Ret: [%ld].", ret);

  if (!NT_SUCCESS(ret))
  {
    HADESMEM_DETAIL_TRACE_NOISY_A("Failed.");
  }

  DWORD const pid = ::GetProcessId(process);
  if (!pid || pid != ::GetCurrentProcessId())
  {
    HADESMEM_DETAIL_TRACE_NOISY_FORMAT_A("Unkown or different process [%lu].",
                                         pid);
    return ret;
  }

  HADESMEM_DETAIL_TRACE_NOISY_A("Succeeded. Current process.");

  auto& callbacks = GetOnUnmapCallbacks();
  callbacks.Run(reinterpret_cast<HMODULE>(base));

  return ret;
}
}

namespace hadesmem
{
namespace cerberus
{
ModuleInterface& GetModuleInterface() HADESMEM_DETAIL_NOEXCEPT
{
  static ModuleImpl module_impl;
  return module_impl;
}

void InitializeModule()
{
  auto& helper = GetHelperInterface();
  helper.InitializeSupportForModule(
    L"NTDLL", DetourNtdllForModule, UndetourNtdllForModule, GetNtdllModule);
}

void DetourNtdllForModule(HMODULE base)
{
  auto const& process = GetThisProcess();
  auto& module = GetNtdllModule();
  auto& helper = GetHelperInterface();
  if (helper.CommonDetourModule(process, L"ntdll", base, module))
  {
    DetourFunc(process,
               base,
               "NtMapViewOfSection",
               GetNtMapViewOfSectionDetour(),
               NtMapViewOfSectionDetour);
    DetourFunc(process,
               base,
               "NtUnmapViewOfSection",
               GetNtUnmapViewOfSectionDetour(),
               NtUnmapViewOfSectionDetour);
  }
}

void UndetourNtdllForModule(bool remove)
{
  auto& module = GetNtdllModule();
  auto& helper = GetHelperInterface();
  if (helper.CommonUndetourModule(L"ntdll", module))
  {
    UndetourFunc(L"NtMapViewOfSection", GetNtMapViewOfSectionDetour(), remove);
    UndetourFunc(
      L"NtUnmapViewOfSection", GetNtUnmapViewOfSectionDetour(), remove);

    module = std::make_pair(nullptr, 0);
  }
}
}
}
