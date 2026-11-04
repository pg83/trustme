#![feature(type_alias_impl_trait)]
type First = impl Copy;
type Second = impl Copy;
#[define_opaque(First)]
fn first() -> First { 251u16 }
#[define_opaque(Second)]
fn second() -> Second { 257u16 }
struct Wrap<T>(T);
trait Stamp<U> { fn stamp(&self) -> u16; }
impl Stamp<u32> for Wrap<First> {
    fn stamp(&self) -> u16 { 263 }
}
fn main() {
    assert_eq!(Wrap(first()).stamp(), 263);
    let _ = second();
}
