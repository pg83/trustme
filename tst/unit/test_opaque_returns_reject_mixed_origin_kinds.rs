//@ compile-fail: different erased class
#![feature(type_alias_impl_trait)]
mod alias {
    pub type Token = impl Copy;
    #[define_opaque(Token)] pub fn make() -> Token { 1u8 }
}
fn opaque() -> impl Copy { 1u8 }
fn same<T: Copy>(_: T, _: T) {}
fn main() { same(alias::make(), opaque()); }
