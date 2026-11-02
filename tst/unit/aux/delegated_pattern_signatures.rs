#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]
#![feature(generic_const_exprs, const_trait_impl, pattern_type_range_trait)]
pub type Split = std::pat::pattern_type!(i16 is ..=-3 | 3..);
pub type Wide = std::pat::pattern_type!(u128 is 0x10000000000000000000000000..=0x10000000000000000000000003);
pub trait Patterns {
    fn split(&self, value: Split) -> i16 { unsafe { std::mem::transmute(value) } }
    fn window<const LOW: u16, const HIGH: u16>(&self, value: std::pat::pattern_type!(u16 is LOW..HIGH)) -> u16 { unsafe { std::mem::transmute(value) } }
    fn wide(&self, value: Wide) -> u128 { unsafe { std::mem::transmute(value) } }
}
pub struct Source;
impl Patterns for Source {}
