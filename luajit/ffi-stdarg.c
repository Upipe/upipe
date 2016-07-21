#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define va(Type) \
    else if (!strcmp(type, #Type)) \
        return (intptr_t) va_arg(*ap, Type)

intptr_t ffi_va_arg(va_list *ap, const char *type)
{
    if (type[strlen(type) - 1] == '*')
        return (intptr_t) va_arg(*ap, void *);
    va(int);
    va(unsigned int);
    va(signed int);
    va(uint32_t);
    va(uint64_t);
    else abort();
}
