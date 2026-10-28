// Address: 0x8000566c | Name: func_text_566c | Date: 2026-07-23
// Description: Boot sequence wrapper. Calls func_text_57c0 (heap allocation init)
// then stub_6028 (likely an OS/thread init stub).

#include "common.h"

extern void func_text_57c0(/* many forwarded params */);
extern void stub_6028(void);

void func_text_566c(/* forwarded params */) {
    func_text_57c0(/* forward all params */);
    stub_6028();
}
