//@ edition: 2018
#![feature(rustc_attrs)]
#[rustc_skip_during_method_dispatch(array, boxed_slice)]
trait Owned { fn stamp(self) -> u8; }
impl Owned for [u8; 2] { fn stamp(self) -> u8 { 1 } }
impl Owned for Box<[u8]> { fn stamp(self) -> u8 { 2 } }
trait View { fn stamp(&self) -> u8; }
impl View for [u8] { fn stamp(&self) -> u8 { 3 } }
fn main() {
    let array = [5, 7];
    let boxed: Box<[u8]> = vec![11, 13].into_boxed_slice();
    assert_eq!(array.stamp(), 3);
    assert_eq!(boxed.stamp(), 3);
    assert_eq!(Owned::stamp(array), 1);
    assert_eq!(Owned::stamp(boxed), 2);
}
