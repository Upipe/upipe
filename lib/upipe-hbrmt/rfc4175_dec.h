/* process (15*mmsize)/16 bytes per iteration */
void upipe_sdi_v210_unpack_ssse3(const uint8_t *src, uint32_t *dst, int64_t size);
void upipe_sdi_v210_unpack_avx(const uint8_t *src, uint32_t *dst, int64_t size);
void upipe_sdi_v210_unpack_avx2(const uint8_t *src, uint32_t *dst, int64_t size);
