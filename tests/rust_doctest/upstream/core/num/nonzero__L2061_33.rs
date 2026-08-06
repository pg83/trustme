// Extracted from library/core/src/num/nonzero.rs:2061
use std::num::NonZero;

fn main() { test().unwrap(); }
fn test() -> Option<()> {






assert_eq!(pos_five.saturating_neg(), neg_five);
assert_eq!(min.saturating_neg(), max);
assert_eq!(max.saturating_neg(), min_plus_one);
Some(())
}
