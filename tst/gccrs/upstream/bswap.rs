// { dg-do run }
// { dg-options "-O2" }

fn gccrs_main() -> i32 {
    assert_eq!(0x0102_u16.swap_bytes(), 0x0201);
    assert_eq!(0x0102_i16.swap_bytes(), 0x0201);
    assert_eq!(0x01020304_u32.swap_bytes(), 0x04030201);
    assert_eq!(0x01020304_i32.swap_bytes(), 0x04030201);
    assert_eq!(0x0102030405060708_u64.swap_bytes(), 0x0807060504030201);
    assert_eq!(0x0102030405060708_i64.swap_bytes(), 0x0807060504030201);
    assert_eq!(
        0x0102030405060708090a0b0c0d0e0f10_u128.swap_bytes(),
        0x100f0e0d0c0b0a090807060504030201,
    );
    assert_eq!(
        0x0102030405060708090a0b0c0d0e0f10_i128.swap_bytes(),
        0x100f0e0d0c0b0a090807060504030201,
    );
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
