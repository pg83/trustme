// Extracted from library/core/src/num/nonzero.rs:1850
use std::num::NonZero;

fn main() { test().unwrap(); }
fn test() -> Option<()> {






assert_eq!(pos, pos.saturating_abs());
assert_eq!(pos, neg.saturating_abs());
assert_eq!(max, min.saturating_abs());
assert_eq!(max, min_plus.saturating_abs());
Some(())
}
