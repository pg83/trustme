//@ crate-type: lib
#![feature(c_variadic)]

#[no_mangle]
pub unsafe extern "C" fn cov99_w1103_variadic_five_fixed(
    a: i32,
    b: i32,
    c: i32,
    d: i32,
    e: i32,
    _: ...
) -> i32 {
    a + b + c + d + e
}
