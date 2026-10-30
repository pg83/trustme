//@ edition: 2024
#![feature(type_alias_impl_trait)]
type Implicit = impl Copy;
type Explicit = impl Copy + use<>;
trait Code<const ID: u8> { fn code(self) -> u8; }
impl Code<1> for Implicit { fn code(self) -> u8 { 19 } }
impl Code<2> for Explicit { fn code(self) -> u8 { 29 } }
#[define_opaque(Implicit)]
fn implicit() -> Implicit { 7u8 }
#[define_opaque(Explicit)]
fn explicit() -> Explicit { 7u16 }
fn main() {
    assert_eq!(<Implicit as Code<1>>::code(implicit()), 19);
    assert_eq!(<Explicit as Code<2>>::code(explicit()), 29);
}
