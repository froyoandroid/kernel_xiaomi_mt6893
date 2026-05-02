#include <stdio.h>
#include "include/uapi/linux/v4l2-controls.h"

#define V4L2_CTRL_CLASS_MPEG 0x00990000

int main() {
    printf("BASE: 0x%lx\n", (unsigned long)V4L2_CID_MPEG_MTK_BASE);
    printf("INTERLACING: 0x%lx (BASE+%ld)\n", (unsigned long)V4L2_CID_MPEG_MTK_INTERLACING, (unsigned long)(V4L2_CID_MPEG_MTK_INTERLACING - V4L2_CID_MPEG_MTK_BASE));
    printf("CODEC_TYPE: 0x%lx (BASE+%ld)\n", (unsigned long)V4L2_CID_MPEG_MTK_CODEC_TYPE, (unsigned long)(V4L2_CID_MPEG_MTK_CODEC_TYPE - V4L2_CID_MPEG_MTK_BASE));
    return 0;
}
