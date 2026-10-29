//@ proc-macro-aux-build: literal_roundtrip.rs
use literal_roundtrip::{constructed, forwarded, reparsed};

fn main() {
    let values = constructed!();
    assert_eq!(values.0, u128::MAX);
    assert_eq!(values.1, i128::MIN);
    assert_eq!(values.2, -2.5f32);
    assert_eq!(values.3, 0.125f64);
    assert_eq!(values.4, "a\nλ");
    assert_eq!(values.5, b"\0\xff\"");
    assert_eq!(values.6, 'λ');

    let original = (0xffu16, -123i32, 6.25e-2f64, b"a\0", 'z', "text");
    let passed = forwarded!((0xffu16, -123i32, 6.25e-2f64, b"a\0", 'z', "text"));
    let parsed = reparsed!((0xffu16, -123i32, 6.25e-2f64, b"a\0", 'z', "text"));
    assert_eq!(passed, original);
    assert_eq!(parsed, original);
}
