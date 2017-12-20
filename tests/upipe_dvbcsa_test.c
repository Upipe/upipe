#include <upipe/ubase.h>

#include <upipe-dvbcsa/upipe_dvbcsa_common.h>

int main(int argc, char *argv[])
{
    struct ustring_dvbcsa_cw cw =
        ustring_to_dvbcsa_cw(ustring_from_str("112233445566"));
    assert(cw.str.len == 12);

    cw = ustring_to_dvbcsa_cw(ustring_from_str("1122330044556600"));
    assert(ustring_is_null(cw.str));

    cw = ustring_to_dvbcsa_cw(ustring_from_str("11223366445566FF"));
    assert(cw.str.len == 16);
    return 0;
}
