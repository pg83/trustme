// Extracted from library/core/src/num/nonzero.rs:2031
use std::num::NonZero;

fn main() { test().unwrap(); }
fn test() -> Option<()> {




assert_eq!(pos_five.overflowing_neg(), (neg_five, false));
assert_eq!(min.overflowing_neg(), (min, true));
Some(())
}
