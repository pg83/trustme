// Extracted from library/core/src/num/wrapping.rs:686
#![allow(unused)]
fn main() {
    use std::num::Wrapping;

    let n = Wrapping(0b0000000_01010101i16);
    assert_eq!(n, Wrapping(85));

    let m = n.reverse_bits();

    assert_eq!(m.0 as u16, 0b10101010_00000000);
    assert_eq!(m, Wrapping(-22016));
}
