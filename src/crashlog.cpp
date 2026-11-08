// Crash logger: an unhandled exception (access violation, ...) writes its code and an unwound backtrace to
// normandy.log before the process dies, as offsets into the exe ("exe+0x1234"). The release exe is stripped (-s), so
// symbolize the offsets with an unstripped relink of the same objects (docs/lasso.md#crash-logs):
//   llvm-symbolizer --obj=NormandyMapmaker_sym.exe --adjust-vma=<image base> 0x...
// Installed by a static initializer, so it needs no hook in main().
#include "common.h"
#include <windows.h>

namespace {
LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
  static volatile LONG once = 0;
  if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
  HMODULE exe = GetModuleHandleW(nullptr);
  const uintptr_t base = (uintptr_t)exe;
  const EXCEPTION_RECORD* er = ep->ExceptionRecord;
  uintptr_t addr = (uintptr_t)er->ExceptionAddress;
  logf("[crash] exception 0x%08lx at exe+0x%llx (image base 0x%llx, thread %lu)", er->ExceptionCode,
       (unsigned long long)(addr - base), (unsigned long long)base, GetCurrentThreadId());
  if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
    logf("[crash] access violation: %s address 0x%llx", er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "execute",
         (unsigned long long)er->ExceptionInformation[1]);
  // unwind from the faulting context (x64 unwind tables)
  CONTEXT ctx = *ep->ContextRecord;
  for (int k = 0; k < 48 && ctx.Rip; ++k) {
    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
    char mod[MAX_PATH] = "?";
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ctx.Rip, &hm) && hm) {
      GetModuleFileNameA(hm, mod, sizeof mod);
      const char* s = strrchr(mod, '\\'); if (s) memmove(mod, s + 1, strlen(s));
    }
    logf("[crash]   #%02d %s+0x%llx", k, hm == exe ? "exe" : mod, (unsigned long long)(ctx.Rip - (uintptr_t)(hm ? hm : exe)));
    if (!fn) {   // leaf function: the return address is on top of the stack
      ctx.Rip = *(DWORD64*)ctx.Rsp; ctx.Rsp += 8;
    } else {
      void* handlerData = nullptr; DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
    }
  }
  logf("[crash] end of backtrace");
  return EXCEPTION_CONTINUE_SEARCH;
}
struct Install { Install() { SetUnhandledExceptionFilter(crashFilter); } } gInstall;
}   // namespace
