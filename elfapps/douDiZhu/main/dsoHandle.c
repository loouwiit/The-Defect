/*
 * dsoHandle.c — 提供 __dso_handle（预防性）
 *
 * 插件 .so 用 -nostdlib 链接（不链接 crtbeginS.o），GNU ld 对 shared 目标的
 * __dso_handle 引用做硬检查。触发条件：.so 内带析构的函数局部 static
 * （引用 __cxa_atexit(..., &inst, __dso_handle)）。tetris 已踩坑，douDiZhu
 * 预防性同加（含 std::function 成员，可能触发）。
 *
 * 惯例值指向自身（非 0，等价 crtbeginS）。本 loader 不执行析构，仅满足链接。
 */
void* __dso_handle = (void*)&__dso_handle;
