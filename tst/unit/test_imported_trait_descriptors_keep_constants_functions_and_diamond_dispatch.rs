//@ aux-build: diamond_descriptor_exports.rs
//@ compile-fail: Unknown literal prefix
extern crate diamond_descriptor_exports;
use diamond_descriptor_exports::{Base, Left, Right, Joined, Number};
struct Sample(u16);
impl Base for Sample { fn read(&self) -> u16 { self.0 } }
impl Left for Sample {}
impl Right for Sample {}
impl Joined for Sample {}
impl Number for Sample {}
macro_rules! reserved { () => { <Sample as Base>::vtable# }; }
fn main() {
    #[cfg(reserved)] { let _ = reserved!(); }
    let value = Sample(std::hint::black_box(13));
    let view: &dyn Joined = &value;
    assert_eq!(view.left(), 14);
    assert_eq!(view.right(), 26);
    assert_eq!(view.read(), 13);
    assert_eq!(diamond_descriptor_exports::total(&value), 20);
    let read: fn(&Sample) -> u16 = <Sample as Base>::read;
    assert_eq!(read(&value), 13);
    assert_eq!(<Sample as Number>::N, 7);
}
