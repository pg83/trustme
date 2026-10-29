//@ compile-fail: different source
#![feature(type_alias_impl_trait)]
mod left {
    pub type Token = impl Copy;
    #[define_opaque(Token)] pub fn make() -> Token { 1u8 }
}
mod right {
    pub type Token = impl Copy;
    #[define_opaque(Token)] pub fn make() -> Token { 1u8 }
}
fn same<T: Copy>(_: T, _: T) {}
fn main() { same(left::make(), right::make()); }
