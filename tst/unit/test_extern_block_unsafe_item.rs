// Rust 2024 lets an item in an extern block be marked `unsafe` or `safe`
// explicitly. `safe` was already accepted; `unsafe` was an unexpected token
// where `fn`, `static` or `type` was expected.
//
// Same shape as the upstream tests unpretty/extern-static.rs and
// linking/executable-no-mangle-strip.rs.

unsafe extern "C" {
    pub unsafe static environ: *const *const u8;

    unsafe fn abs(v: i32) -> i32;

    safe fn labs(v: i64) -> i64;
}

fn main() {
    assert_eq!(unsafe { abs(-3) }, 3);
    assert_eq!(labs(-4), 4);

    // The static is only referenced, never read, so the program does not depend
    // on the process environment.
    let p: *const *const u8 = unsafe { environ };
    assert!(!p.is_null() || p.is_null());
}
