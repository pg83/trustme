// Extracted from library/core/src/num/nonzero.rs:684
use std::num::NonZero;

fn main() { test().unwrap(); }
fn test() -> Option<()> {



assert_eq!(a.count_ones(), NonZero::new(1)?);
assert_eq!(b.count_ones(), NonZero::new(3)?);
Some(())
}
