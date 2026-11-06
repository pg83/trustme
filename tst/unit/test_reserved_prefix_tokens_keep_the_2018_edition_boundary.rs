//@ aux-build: diamond_descriptor_exports.rs
//@ edition: 2018
//@ reject-flags: --edition=2021 => Unknown literal prefix || prefix
extern crate diamond_descriptor_exports;
use diamond_descriptor_exports::{Base, Left, Right, Joined, Number};
struct Sample(u16);
impl Base for Sample { fn read(&self) -> u16 { self.0 } }
impl Left for Sample {}
impl Right for Sample {}
impl Joined for Sample {}
impl Number for Sample {}
macro_rules! adjacent {
    (token# $value:literal) => { $value + 1 };
    (token"value") => { 12 };
    (token'x') => { 13 };
}
macro_rules! raw_name { ($name:ident # $value:literal) => { $value }; }
fn main() {
    assert_eq!(adjacent!(token# 10), 11);
    assert_eq!(adjacent!(token"value"), 12);
    assert_eq!(adjacent!(token'x'), 13);
    assert_eq!(raw_name!(r#type# 3), 3);
    let value = Sample(std::hint::black_box(13));
    let view: &dyn Joined = &value;
    assert_eq!(view.left(), 14);
    assert_eq!(view.right(), 26);
    assert_eq!(view.read(), 13);
    assert_eq!(diamond_descriptor_exports::total(&value), 20);
    let read: fn(&Sample) -> u16 = <Sample as Base>::read;
    assert_eq!(read(&value), 13);
    assert_eq!(<Sample as Number>::N, 7);
    assert_eq!(b'x', 120);
    assert_eq!(br#"ok"#, b"ok");
    assert_eq!(r#"ok"#, "ok");
}
