//@ compile-variant: -O
#![feature(type_alias_impl_trait)]
type Key = impl Copy + PartialEq;
#[define_opaque(Key)]
const KEY: Key = 17u8;
const WIDTH: usize = std::mem::size_of::<Key>();
fn matches(value: usize) -> bool { match value { WIDTH => true, _ => false } }
fn main() {
    assert!(matches(1));
    assert!(!matches(2));
    assert!(KEY == KEY);
}
