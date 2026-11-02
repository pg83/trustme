//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro)]
macro_rules! alias_and_spell {
    ($name:ident, $text:ident, $ty:ty) => { type $name = $ty; const $text: &str = stringify!($ty); };
}
alias_and_spell!(Callback, SPELLING, fn(std::pat::pattern_type!(u16 is 3..=9)) -> ());
fn main() {
    assert!(SPELLING.contains("pattern_type"));
    let callback: Callback = |_| {};
    callback(5);
}
