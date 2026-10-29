/* Test-only GPIO device for the standalone matrix diagnostic. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <linux/gpio.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

int ioctl(int fd, unsigned long request, ...) {
    static int lines=-1;
    static unsigned state=240, writes;
    va_list args; va_start(args,request); void *arg=va_arg(args,void *); va_end(args);
    if (request==GPIO_V2_GET_LINE_IOCTL) {
        struct gpio_v2_line_request *r=arg;
        if (r->num_lines!=8 || lines>=0) abort();
        const unsigned pins[]={7,4,8,9,10,11,18,15};
        for (unsigned i=0;i<8;i++) if (r->offsets[i]!=pins[i]) abort();
        if (r->config.flags!=GPIO_V2_LINE_FLAG_OUTPUT || r->config.attrs[0].attr.values!=240) abort();
        r->fd=lines=dup(fd); fprintf(stderr,"TEST_GPIO_MATRIX_ONLY\n"); return lines<0?-1:0;
    }
    if (request!=GPIO_V2_LINE_SET_VALUES_IOCTL || fd!=lines) abort();
    struct gpio_v2_line_values *v=arg;
    if (v->mask!=15 && v->mask!=240) abort();
    const char *fail=getenv("SAMPLER_TEST_GPIO_FAIL");
    if (++writes==2 && fail) { errno=EIO; return -1; }
    if (v->mask==240 && (state&15)) abort();
    state=(state&~v->mask)|(v->bits&v->mask);
    if (state==240) fprintf(stderr,"TEST_GPIO_OFF\n");
    return 0;
}
