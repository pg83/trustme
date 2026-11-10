//@ compile-flags: -O -C panic=abort

extern "C" {
    fn abs(value: i32) -> i32;
    fn tolower(value: i32) -> i32;
}

#[inline]
fn fold_signed_ascii(value: i32) -> i32 {
    unsafe {
        let magnitude = abs(value);
        tolower(magnitude)
    }
}

fn main() {
    let value = std::hint::black_box(-i32::from(b'G'));
    assert_eq!(fold_signed_ascii(value), i32::from(b'g'));
}
