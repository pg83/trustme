//@ compile-fail: error:
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]
trait FixedElement {
    type Value;
}
impl FixedElement for [u8; 47] {
    type Value = u32;
}
const fn unresolved_box_length() -> usize {
    let container: Box<_> = Default::default();
    let _ = container;
    47
}
type Early = <[u8; unresolved_box_length()]>::Value;
fn main() {}
