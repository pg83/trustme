/* Compiled as C, where an empty struct occupies no space and is passed in
 * nothing at all -- which is what Rust's C ABI does with a zero-sized
 * argument.  Each check reads the argument that follows the empty one. */
struct empty {};

struct pair {
    int a;
    int b;
};

int extern_c_zst_between(struct pair first, struct empty gap, struct pair second) {
    (void)gap;
    return first.a * 1000 + first.b * 100 + second.a * 10 + second.b;
}

int extern_c_zst_leading(struct empty gap, int value) {
    (void)gap;
    return value;
}

int extern_c_zst_only(struct empty gap) {
    (void)gap;
    return 17;
}
