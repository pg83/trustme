#![feature(type_alias_impl_trait)]
pub type First = impl Copy;
pub type Second = impl Copy;
#[define_opaque(First)]
pub fn first() -> First { 191u16 }
#[define_opaque(Second)]
pub fn second() -> Second { 197u16 }
pub struct Wrap<T>(pub T);
pub trait Stamp<U> { fn stamp(&self) -> u16; }
impl Stamp<u32> for Wrap<First> {
    fn stamp(&self) -> u16 { 193 }
}
