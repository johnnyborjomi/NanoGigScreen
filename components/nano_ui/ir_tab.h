/* The IR tab of the Capture / IR page (page_source.c builds it into its content box). Private to nano_ui. */
#ifndef IR_TAB_H
#define IR_TAB_H

#include "lvgl.h"

#define SOURCE_TOP (TOP_H + 4)            /* the tabs' content box, under the header */
#define SOURCE_NAME_Y (38 - SOURCE_TOP)   /* the source's name line, content coordinates */

void ir_tab_build(lv_obj_t *content);
void ir_tab_destroy(void);               /* the content is being cleaned or deleted */
bool source_tab_is(int tab);             /* 0 = capture, 1 = IR is built */

#endif
