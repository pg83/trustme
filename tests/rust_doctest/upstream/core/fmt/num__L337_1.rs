// Extracted from library/core/src/fmt/num.rs:337
#![allow(unused)]
#![feature(int_format_into)]
fn main() {
    use core::fmt::NumBuffer;
    
    
    let mut buf = NumBuffer::new();
    assert_eq!(n.format_into(&mut buf), "0");
    
    
    assert_eq!(n1.format_into(&mut buf), "32");
}
