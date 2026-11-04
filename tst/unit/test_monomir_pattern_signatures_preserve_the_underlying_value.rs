//@ compile-only
//@ compile-flags: -Ccodegen-type=monomir
//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro)]

type Small = std::pat::pattern_type!(u8 is 3..=9);

#[inline(never)]
pub fn selected(value: Small) -> Small { value }

fn main() {
    let input: Small = unsafe { std::mem::transmute(7u8) };
    let output: u8 = unsafe { std::mem::transmute(selected(input)) };
    assert_eq!(output, 7);
}
