//@ compile-fail: error:
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]
trait FixedElement {
    type Value;
}
impl FixedElement for [u8; 29] {
    type Value = u16;
}
const fn unresolved_header_length() -> usize {
    let value = Default::default();
    let _ = value;
    29
}
type Early = <[u8; unresolved_header_length()]>::Value;
fn main() {}
