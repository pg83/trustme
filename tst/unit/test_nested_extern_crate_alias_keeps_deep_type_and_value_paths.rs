mod foreign {
    pub extern crate core as primitives;
}
use foreign::primitives::num::NonZeroU16 as Nonzero;
use foreign::primitives::cmp::Ordering::{Equal, Greater};
fn main() {
    let value = Nonzero::new(719).unwrap();
    assert_eq!(value.get(), 719);
    assert_eq!(value.get().cmp(&719), Equal);
    assert_eq!(value.get().cmp(&710), Greater);
}
