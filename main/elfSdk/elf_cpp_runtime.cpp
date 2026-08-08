/*
 * C++ 运行时符号表 — 供 .so 插件链接 libstdc++/libsupc++ 的运行时符号。
 * .so 编译为 -fno-exceptions -fno-rtti 但 std::vector/string 等模板代码仍引用
 * operator new/delete、__cxa_guard_*（函数局部 static）、throw 辅助等。
 * 这些符号定义在主固件（libstdc++/libsupc++ 静态链接进 app），此处取地址注册，
 * 使 .so 的 UND 符号运行时解析到主固件实现。
 */
#include "esp_elf.h"
#include <new>
#include <cstddef>

/* 与 libsupc++ 一致的运行时符号声明（此处仅取地址，不调用） */
namespace std {
    void __throw_bad_alloc();
    void __throw_length_error(const char*);
    void __throw_bad_array_new_length();
    void __throw_logic_error(const char*);
}

extern "C" {
    int  __cxa_guard_acquire(void*);
    void __cxa_guard_release(void*);
    void __cxa_guard_abort(void*);
    int  __cxa_atexit(void (*)(void*), void*, void*);
    void __cxa_pure_virtual();
}

/* operator new / delete（含数组版）— 用包装函数规避 `operator new/delete` 名字
 * 作 primary-expression 的语法限制（只能作 & 的操作数）。包装内部 `::operator
 * new(n)` / `::operator delete(p)` 是普通调用，可正常编译。 */
static void* opNewImpl(size_t n)     { return ::operator new(n); }
static void* opNewArrImpl(size_t n)  { return ::operator new[](n); }
static void  opDelImpl(void* p)      { ::operator delete(p); }
static void  opDelArrImpl(void* p)   { ::operator delete[](p); }
static void  opDelSizedImpl(void* p, size_t n) { ::operator delete(p, n); }   /* sized delete，C++14 */

extern const struct esp_elfsym g_elf_cpp_runtime_syms[] = {
    /* operator new / delete（⚠ size_t 在 32 位目标 = unsigned int → 编码 j，非 m） */
    { "_Znwj",  (void*)&opNewImpl },
    { "_Znaj",  (void*)&opNewArrImpl },
    { "_ZdlPv", (void*)&opDelImpl },
    { "_ZdaPv", (void*)&opDelArrImpl },
    { "_ZdlPvj",(void*)&opDelSizedImpl },

    /* 分配失败 / 长度超限（std::vector/string 内部引用） */
    { "_ZSt17__throw_bad_allocv",        (void*)&std::__throw_bad_alloc },
    { "_ZSt20__throw_length_errorPKc",   (void*)&std::__throw_length_error },
    { "_ZSt28__throw_bad_array_new_lengthv", (void*)&std::__throw_bad_array_new_length },
    { "_ZSt19__throw_logic_errorPKc",    (void*)&std::__throw_logic_error },

    /* 函数局部 static 的线程安全守卫 */
    { "__cxa_guard_acquire", (void*)&__cxa_guard_acquire },
    { "__cxa_guard_release", (void*)&__cxa_guard_release },
    { "__cxa_guard_abort",   (void*)&__cxa_guard_abort },
    { "__cxa_atexit",        (void*)&__cxa_atexit },
    { "__cxa_pure_virtual",  (void*)&__cxa_pure_virtual },

    ESP_ELFSYM_END
};
