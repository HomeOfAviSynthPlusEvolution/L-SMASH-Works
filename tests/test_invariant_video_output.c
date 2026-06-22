#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

/* Test the frame size calculation for integer overflow */
#define MAKE_AVIUTL_PITCH(x) (((x) + 3) & ~3)

START_TEST(test_frame_size_overflow)
{
    /* Invariant: Frame size calculation must not overflow, causing undersized allocation */
    struct {
        uint32_t width;
        uint32_t height;
        uint16_t biBitCount;
        int should_overflow;
    } payloads[] = {
        {65536, 65536, 32, 1},    /* Exploit case: causes overflow */
        {46341, 46341, 32, 1},    /* Boundary: sqrt(INT32_MAX/32) approx */
        {1920, 1080, 32, 0},      /* Valid HD input */
        {4096, 4096, 24, 0},      /* Valid 4K input */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        uint64_t safe_linesize = ((uint64_t)payloads[i].width * payloads[i].biBitCount + 3) & ~3ULL;
        uint64_t safe_frame_size = safe_linesize * payloads[i].height;
        
        /* Simulate the vulnerable 32-bit calculation */
        uint32_t vuln_linesize = MAKE_AVIUTL_PITCH(payloads[i].width * payloads[i].biBitCount);
        uint32_t vuln_frame_size = vuln_linesize * payloads[i].height;
        
        int overflowed = (safe_frame_size != vuln_frame_size) || (safe_frame_size > UINT32_MAX);
        
        if (payloads[i].should_overflow) {
            /* For overflow cases, verify detection would catch it */
            ck_assert_msg(overflowed, 
                "Payload %d: Expected overflow not detected (w=%u h=%u bpp=%u)",
                i, payloads[i].width, payloads[i].height, payloads[i].biBitCount);
        } else {
            /* For valid cases, verify no overflow */
            ck_assert_msg(!overflowed,
                "Payload %d: Unexpected overflow (w=%u h=%u bpp=%u)",
                i, payloads[i].width, payloads[i].height, payloads[i].biBitCount);
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_frame_size_overflow);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}