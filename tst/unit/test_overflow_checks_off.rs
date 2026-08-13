//@ compile-flags: -O -Coverflow-checks=off

#[inline(never)]
fn opaque<T>(value: T) -> T {
    value
}

fn main() {
    use std::panic::catch_unwind;

    assert_eq!(opaque(u8::MAX) + opaque(1), 0);
    assert_eq!(opaque(i8::MIN) - opaque(1), i8::MAX);
    assert_eq!(opaque(u8::MAX) * opaque(2), 254);
    assert_eq!(opaque(i32::MAX) + opaque(1), i32::MIN);
    assert_eq!(opaque(i64::MIN) - opaque(1), i64::MAX);
    assert_eq!(opaque(i64::MAX) * opaque(2), -2);
    assert_eq!(opaque(1u8) << opaque(8u32), 1);
    assert_eq!(opaque(1u8) >> opaque(-1i8), 0);
    assert_eq!(-opaque(i8::MIN), i8::MIN);

    assert!(catch_unwind(|| opaque(i8::MIN) / opaque(-1)).is_err());
    assert!(catch_unwind(|| opaque(i8::MIN) % opaque(-1)).is_err());
    assert!(catch_unwind(|| opaque(1u8) / opaque(0)).is_err());
}
