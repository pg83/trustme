//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro)]
macro_rules! alias_and_spell {
    ($name:ident, $text:ident, $ty:ty) => { type $name = $ty; const $text: &str = stringify!($ty); };
}
alias_and_spell!(Values, SPELLING, dyn Iterator<Item = std::pat::pattern_type!(u8 is 1..=7)>);
fn main() {
    assert!(SPELLING.contains("pattern_type"));
    assert_eq!(core::mem::size_of::<&Values>(), 2 * core::mem::size_of::<usize>());
}
