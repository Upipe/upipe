#define ARGS const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels
void upipe_levelb_to_uyvy_c(ARGS);
void upipe_levelb_to_uyvy_ssse3(ARGS);
void upipe_levelb_to_uyvy_avx(ARGS);
void upipe_levelb_to_uyvy_avx2(ARGS);
void upipe_levelb_to_uyvy_avx512(ARGS);
#undef ARGS
