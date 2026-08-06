// Extracted from library/core/src/num/nonzero.rs:1915
use std::num::NonZero;

fn main() { test().unwrap(); }
fn test() -> Option<()> {






assert_eq!(u_pos, i_pos.unsigned_abs());
assert_eq!(u_pos, i_neg.unsigned_abs());
assert_eq!(u_max, i_min.unsigned_abs());
Some(())
}
