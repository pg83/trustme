// Extracted from library/core/src/num/nonzero.rs:934
#![feature(nonzero_bitwise)]
use std::num::NonZero;

fn main() { test().unwrap(); }
fn test() -> Option<()> {


if cfg!(target_endian = "little") {
    assert_eq!(n.to_le(), n)
} else {
    assert_eq!(n.to_le(), n.swap_bytes())
}
Some(())
}
