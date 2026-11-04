//@ compile-fail: error
#![feature(type_alias_impl_trait)]
type First = impl Copy;
type Second = impl Copy;
#[define_opaque(First)]
fn first() -> First { 269u16 }
#[define_opaque(Second)]
fn second() -> Second { 271u16 }
struct Wrap<T>(T);
trait Stamp<U> { fn stamp(&self) -> u16; }
impl Stamp<u32> for Wrap<First> {
    fn stamp(&self) -> u16 { 277 }
}
fn main() {
    let _ = first();
    let _ = Wrap(second()).stamp();
}
