/*
 * dsoHandle.c — 提供 __dso_handle
 *
 * 插件 .so 用 -nostdlib 链接（不链接 crtbeginS.o），而 GNU ld 对 shared 目标的
 * __dso_handle 引用做硬检查（undefined reference），不是普通可留空的 UND。
 * 触发条件：.so 内带析构的函数局部 static（如 getS() 里 static S inst，S 含
 * std::function）→ 引用 __cxa_atexit(..., &inst, __dso_handle)。
 *
 * 惯例值指向自身（非 0，等价 crtbeginS）。本 loader 不执行析构（dlclose 只
 * free 内存），此符号仅满足链接 + 作为 __cxa_atexit 第三参。
 */
void* __dso_handle = (void*)&__dso_handle;
