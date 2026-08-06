// Extracted from library/core/src/num/nonzero.rs:901
#![feature(nonzero_bitwise)]
use std::num::NonZero;

fn main() { test().unwrap(); }
fn test() -> Option<()> {


if cfg!(target_endian = "big") {
    assert_eq!(n.to_be(), n)
} else {
    assert_eq!(n.to_be(), n.swap_bytes())
}
Some(())
}
