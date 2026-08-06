// Extracted from library/core/src/num/nonzero.rs:1883
use std::num::NonZero;

fn main() { test().unwrap(); }
fn test() -> Option<()> {





assert_eq!(pos, pos.wrapping_abs());
assert_eq!(pos, neg.wrapping_abs());
assert_eq!(min, min.wrapping_abs());
assert_eq!(max, (-max).wrapping_abs());
Some(())
}
