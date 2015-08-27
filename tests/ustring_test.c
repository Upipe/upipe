#include <upipe/ubase.h>
#include <upipe/ustring.h>

#include <assert.h>
#include <stdlib.h>

#define ALPHA_LOWER 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', \
                    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', \
                    'u', 'v', 'w', 'x', 'y', 'z'
#define ALPHA_UPPER 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', \
                    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', \
                    'U', 'V', 'W', 'X', 'Y', 'Z'
#define ALPHA ALPHA_LOWER, ALPHA_UPPER
#define DIGIT '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
#define WORD ALPHA, DIGIT

int main(int argc, char *argv[])
{
    assert(ustring_is_null(ustring_null()));
    assert(ustring_is_null(ustring_from_str(NULL)));
    assert(!ustring_is_null(ustring_from_str("")));
    assert(
        ustring_is_empty(
            ustring_shift_until(
                ustring_from_str("this is a string !"), "?")));
    assert(
        ustring_match_str(
            ustring_shift_until(
                ustring_from_str("this is a string !"), "?!"),
            "!"));
    assert(
        ustring_match_str(
            ustring_shift_until(
                ustring_from_str("this is a string !"), "?!a"),
            "a string !"));
    assert(
        ustring_match_sfx(
            ustring_from_str("this is a string !"),
            ustring_from_str("a string !")));
    assert(
        !ustring_match_sfx(
            ustring_from_str("this is a string !"),
            ustring_from_str("a string")));

    const char *strings[] = {
        NULL,
        "",
        "a string",
        "this is a string",
        "This is a STRING",
        "this is a string this is a string",
        "[this] [is] [a] [string]",
        "-this-is-a-string-",
    };
    size_t len_max = 0;
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(strings); i++) {
        if (strings[i] && strlen(strings[i]) > len_max)
            len_max = strlen(strings[i]);
    }

    for (size_t i = 0; i < UBASE_ARRAY_SIZE(strings); i++) {
        struct ustring ustring = ustring_from_str(strings[i]);

        printf("strings[%zu]: \"%s\"\n", i, strings[i]);

        char *str;
        ubase_assert(ustring_to_str(ustring, &str));
        if (!strings[i])
            assert(str == NULL);
        else {
            assert(strcmp(strings[i], str) == 0);
            free(str);
        }

        for (unsigned j = 0; j < len_max; j++) {
            struct ustring shift = ustring_shift(ustring, j);
            ubase_assert(ustring_to_str(shift, &str));
            if (!strings[i] || strlen(strings[i]) < j) {
                assert(str == NULL);
                break;
            }

            assert(str != NULL);
            assert(strcmp(str, strings[i] + j) == 0);
            free(str);
        }

        for (unsigned j = 0; j < len_max; j++) {
            struct ustring trunc = ustring_truncate(ustring, j);
            ubase_assert(ustring_to_str(trunc, &str));
            if (!strings[i]) {
                assert(str == NULL);
                break;
            }

            assert(str != NULL);
            assert(strncmp(str, strings[i], strlen(str)) == 0);
            free(str);
        }

        struct ustring tmp = ustring;
        while (tmp.len) {
            const char word_set[] = { WORD, 0 };
            struct ustring word = ustring_while(tmp, word_set);
            if (word.len) {
                char word_buffer[word.len + 1];
                ustring_cpy(word, word_buffer, sizeof (word_buffer));
                printf("word: \"%s\"\n", word_buffer);
                tmp = ustring_shift(tmp, word.len);
            }

            struct ustring not_word =
                ustring_until(tmp, word_set);
            if (not_word.len) {
                char not_word_buffer[not_word.len + 1];
                ustring_cpy(not_word, not_word_buffer,
                               sizeof (not_word_buffer));
                printf("not word: \"%s\"\n", not_word_buffer);
                tmp = ustring_shift(tmp, not_word.len);
            }
        }

        tmp = ustring;
        while (!ustring_is_null(tmp)) {
            struct ustring l = ustring_split_sep(&tmp, " -");
            if (!ustring_is_null(tmp)) {
                char left[l.len + 1], right[tmp.len + 1];
                ustring_cpy(l, left, sizeof (left));
                ustring_cpy(tmp, right, sizeof (right));
                printf("split \"%s\" and \"%s\"\n", left, right);
            }
        }

        for (size_t j = 0; j < UBASE_ARRAY_SIZE(strings); j++) {
            printf("compare with %s\n", strings[j]);

            struct ustring s1 = ustring_from_str(strings[i]);
            struct ustring s2 = ustring_from_str(strings[j]);
            if (strings[i] && strings[j]) {
                int ret = strcmp(strings[i], strings[j]);
                if (ret == 0)
                    assert(ustring_cmp(s1, s2) == 0);
                else if (ret < 0)
                    assert(ustring_cmp(s1, s2) < 0);
                else
                    assert(ustring_cmp(s1, s2) > 0);
            }
            else {
                if ((!strings[i] && !strings[j]) ||
                    (!strings[i] && strings[j] && !strlen(strings[j])) ||
                    (strings[i] && !strlen(strings[i]) && !strings[j]))
                    assert(ustring_cmp(s1, s2) == 0);
                else
                    assert(ustring_cmp(s1, s2) != 0);
            }

            if (strings[i] && strings[j]) {
                int ret = strcasecmp(strings[i], strings[j]);
                if (ret == 0)
                    assert(ustring_casecmp(s1, s2) == 0);
                else if (ret < 0)
                    assert(ustring_casecmp(s1, s2) < 0);
                else
                    assert(ustring_casecmp(s1, s2) > 0);
            }
            else {
                if ((!strings[i] && !strings[j]) ||
                    (!strings[i] && strings[j] && !strlen(strings[j])) ||
                    (strings[i] && !strlen(strings[i]) && !strings[j]))
                    assert(ustring_casecmp(s1, s2) == 0);
                else
                    assert(ustring_casecmp(s1, s2) != 0);
            }
        }

        if (strings[i] && !strncmp(strings[i], "this", strlen("this")))
            assert(ustring_match(ustring_from_str(strings[i]),
                                 ustring_from_str("this")));
        else
            assert(!ustring_match(ustring_from_str(strings[i]),
                                  ustring_from_str("this")));
    }
    return 0;
}
