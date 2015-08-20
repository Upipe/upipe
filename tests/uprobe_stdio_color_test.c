#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio_color.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(int argc, char **argv)
{
    struct uprobe *uprobe1 =
        uprobe_stdio_color_alloc(NULL, stdout, UPROBE_LOG_DEBUG);
    assert(uprobe1 != NULL);

    uprobe_err(uprobe1, NULL, "This is an error");
    uprobe_warn_va(uprobe1, NULL, "This is a %s warning with %d", "composite",
                   0x42);
    uprobe_notice(uprobe1, NULL, "This is a notice");
    uprobe_dbg(uprobe1, NULL, "This is a debug");
    uprobe_release(uprobe1);

    struct uprobe *uprobe2 =
        uprobe_stdio_color_alloc(NULL, stdout, UPROBE_LOG_ERROR);
    assert(uprobe2 != NULL);
    uprobe_err_va(uprobe2, NULL, "This is another error with %d", 0x43);
    uprobe_warn(uprobe2, NULL, "This is a warning that you shouldn't see");
    uprobe_release(uprobe2);
    return 0;
}
