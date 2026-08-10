// Extracted from library/core/src/fmt/num.rs:789
#![allow(unused)]
#![feature(int_format_into)]
fn main() {
    use core::fmt::NumBuffer;

    let n = 0u128;
    let mut buf = NumBuffer::new();
    assert_eq!(n.format_into(&mut buf), "0");

    let n1 = 32u128;
    let mut buf1 = NumBuffer::new();
    assert_eq!(n1.format_into(&mut buf1), "32");

    let n2 = u128::MAX;
    let mut buf2 = NumBuffer::new();
    assert_eq!(n2.format_into(&mut buf2), u128::MAX.to_string());
}
