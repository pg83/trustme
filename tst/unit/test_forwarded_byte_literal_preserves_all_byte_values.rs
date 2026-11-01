//@ proc-macro-aux-build: forward_byte_literal.rs

macro_rules! forward {
    ($value:expr) => { forward_byte_literal::keep_literal!($value) };
}

fn main() {
    let value = forward!(b"\0\x01\x7f\x80\xff\n\"\\");
    assert_eq!(*value, [0, 1, 127, 128, 255, 10, 34, 92]);
    let raw = forward!(br##"one"#two\three"##);
    assert_eq!(raw, b"one\"#two\\three");
}
