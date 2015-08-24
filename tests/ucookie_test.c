#include <upipe/ucookie.h>

#include <assert.h>

int main(int argc, char *argv[])
{
    const char *cookies[] = {
        "SID=31d4d96e407aad42",
        "SID=31d4d96e407aad42; Path=/; Domain=example.com",
        "SID=31d4d96e407aad42; Path=/; Secure; HttpOnly",
        "lang=en-US; Path=/; Domain=example.com",
        "lang=en-US; Expires=Wed, 09 Jun 2021 10:18:14 GMT",
        "ts=402904; expires=Mon, 23-Jun-2025 13:47:11 GMT; Max-Age=315619200; path=/; domain=.example.com",
        "dmvk=5589635f60427; path=/; domain=.example.com",
        "v1st=01D7ED8D5B92EB29; expires=Wed, 22 Jun 2016 13:47:11 GMT; max-age=31536000; path=/; domain=.example.com",
    };

    for (size_t i = 0; i < UBASE_ARRAY_SIZE(cookies); i++) {
        struct ucookie ucookie = ucookie_null();

        ubase_assert(ucookie_from_str(&ucookie, cookies[i]));
        printf("Cookie: %.*s=%.*s\n",
               (int)ucookie.name.len, ucookie.name.at,
               (int)ucookie.value.len, ucookie.value.at);
        if (ucookie.expires.at)
            printf("\tExpires= %.*s\n",
                   (int)ucookie.expires.len, ucookie.expires.at);
        if (ucookie.max_age.at)
            printf("\tMax-Age= %.*s\n",
                   (int)ucookie.max_age.len, ucookie.max_age.at);
        if (ucookie.domain.at)
            printf("\tDomain= %.*s\n",
                   (int)ucookie.domain.len, ucookie.domain.at);
        if (ucookie.path.at)
            printf("\tPath= %.*s\n",
                   (int)ucookie.path.len, ucookie.path.at);
        if (ucookie.secure)
            printf("\tSecure\n");
        if (ucookie.http_only)
            printf("\tHttpOnly\n");
    }

    return 0;
}
