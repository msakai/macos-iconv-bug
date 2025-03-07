#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <iconv.h>

int main(void)
{
    char result[1024] = "";
    iconv_t cd = iconv_open("SHIFT_JIS", "UTF-32LE");
    int32_t src_buffer[6];
    char dst_buffer[14];
    char* src;
    char* dst;
    size_t src_left;
    size_t dst_left;

    // First iconv() call
    src_buffer[0] = 12371; // こ
    src_buffer[1] = 12435; // ん
    src_buffer[2] = 12395; // に
    src_buffer[3] = 12385; // ち
    src_buffer[4] = 12399; // は
    src_buffer[5] = 'A';
    src = (void*)&src_buffer;
    dst = (void*)&dst_buffer;
    src_left = sizeof(src_buffer);
    dst_left = sizeof(dst_buffer);
    printf("src_left=%zd dst_left=%zd\n", src_left, dst_left);
    // src_left=24 dst_left=14
    size_t ret = iconv(cd, &src, &src_left, &dst, &dst_left);
    assert(ret == 0);
    printf("=> ret=%zd src_left=%zd dst_left=%zd\n", ret, src_left, dst_left);
    // => ret=0 src_left=0 dst_left=3

    // Second iconv() call which results in E2BIG
    src_buffer[0] = 'B';
    src_buffer[1] = 'C';
    src_buffer[2] = 19990; // 世
    src_buffer[3] = 30028; // 界
    src_buffer[4] = 'X';
    src_buffer[5] = 'Y';
    src = (void*)&src_buffer;
    src_left = sizeof(src_buffer);
    printf("src_left=%zd dst_left=%zd\n", src_left, dst_left);
    // src_left=24 dst_left=3
    ret = iconv(cd, &src, &src_left, &dst, &dst_left); // "BC" IS CONSUMED HERE!
    assert(ret == -1);
    assert(errno == E2BIG);
    printf("=> ret=%zd errno=%d src_left=%zd dst_left=%zd\n", ret, errno, src_left, dst_left);
    // => ret=-1 errno=7 src_left=16 dst_left=3
    errno = 0;

    // Flush output buffer
    strncat(result, dst_buffer, dst - dst_buffer);
    dst = (void*)&dst_buffer;
    dst_left = sizeof(dst_buffer);

    // Third icon() call
    printf("src_left=%zd dst_left=%zd\n", src_left, dst_left);
    // src_left=16 dst_left=14
    ret = iconv(cd, &src, &src_left, &dst, &dst_left);
    assert(ret == 0);
    printf("=> ret=%zd errno=%d src_left=%zd dst_left=%zd\n", ret, errno, src_left, dst_left);
    // => ret=0 errno=0 src_left=0 dst_left=8

    // Flush internal state
    printf("src_left=%zd dst_left=%zd\n", src_left, dst_left);
    // src_left=0 dst_left=8
    ret = iconv(cd, (char**)NULL, (size_t*)NULL, &dst, &dst_left);
    assert(ret == 0);
    printf("=> ret=%zd errno=%d src_left=%zd dst_left=%zd\n", ret, errno, src_left, dst_left);
    // => ret=0 errno=0 src_left=0 dst_left=8

    // Flush output buffer and finish conversion
    strncat(result, dst_buffer, dst - dst_buffer);
    iconv_close(cd);

    // Dump result to file
    FILE *f = fopen("result.txt", "w");
    fputs(result, f);
    fclose(f);

    // Comparison with expected result
    char *expected = "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd" "ABC" "\x90\xa2\x8a\x45" "XY";
    if (strcmp(result, expected)) {
        printf("FAIL\n");
        return 1;
    } else {
        printf("PASS\n");
        return 0;
    }
}
