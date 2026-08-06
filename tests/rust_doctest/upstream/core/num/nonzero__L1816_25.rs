// Extracted from library/core/src/num/nonzero.rs:1816
use std::num::NonZero;

fn main() { test().unwrap(); }
fn test() -> Option<()> {




assert_eq!((pos, false), pos.overflowing_abs());
assert_eq!((pos, false), neg.overflowing_abs());
assert_eq!((min, true), min.overflowing_abs());
Some(())
}
