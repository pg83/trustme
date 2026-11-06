//@ aux-build: diamond_descriptor_exports.rs
//@ compile-fail: Type mismatch between
extern crate diamond_descriptor_exports;
use diamond_descriptor_exports::{Base, Left, Right, Joined, Number};
struct Sample(u16);
impl Base for Sample { fn read(&self) -> u16 { self.0 } }
impl Left for Sample {}
impl Right for Sample {}
impl Joined for Sample {}
impl Number for Sample {}
macro_rules! raw_name { ($name:ident # $value:literal) => { $value }; }
fn main() {
    let value = Sample(std::hint::black_box(13));
    let view: &dyn Joined = &value;
    assert_eq!((view.left(), view.right(), view.read()), (14, 26, 13));
    assert_eq!(diamond_descriptor_exports::total(&value), 20);
    let read: fn(&Sample) -> u16 = <Sample as Base>::read;
    assert_eq!(read(&value), 13);
    assert_eq!(<Sample as Number>::N, 7);
    assert_eq!(raw_name!(r#type# 3), 3);
    assert_eq!((b'x', br#"ok"#, r#"ok"#), (120, &b"ok"[..], "ok"));
}
