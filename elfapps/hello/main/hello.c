/*
 * Phase 0 链路验证用 .so 插件。
 * - hello_world() 是导出符号，主固件 dlsym("hello_world") 调用。
 * - printf 是外部符号（UND），运行时由主固件 newlib 符号表解析（原生直出）。
 */
#include <stdio.h>

int hello_world(void)
{
    printf("hello from ELF .so plugin!\n");
    return 42;
}
