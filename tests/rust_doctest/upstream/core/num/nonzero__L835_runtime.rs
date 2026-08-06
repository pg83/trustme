// Extracted from library/core/src/num/nonzero.rs:835
#![feature(nonzero_bitwise)]
use std::num::NonZero;


fn main() { test().unwrap(); }
fn test() -> Option<()> {


if cfg!(target_endian = "big") {

} else {

}
Some(())
}
