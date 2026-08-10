// Extracted from library/core/src/fmt/num.rs:824
#![allow(unused)]
#![feature(int_format_into)]
fn main() {
    use core::fmt::NumBuffer;

    let n = 0i128;
    let mut buf = NumBuffer::new();
    assert_eq!(n.format_into(&mut buf), "0");

    let n1 = i128::MIN;
    assert_eq!(n1.format_into(&mut buf), i128::MIN.to_string());

    let n2 = i128::MAX;
    assert_eq!(n2.format_into(&mut buf), i128::MAX.to_string());
}
