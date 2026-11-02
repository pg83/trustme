#![feature(pattern_types, pattern_type_macro)]
pub type Small = std::pat::pattern_type!(u16 is 3..=9);
pub trait Signatures {
    fn bounded(&self, value: Small) -> u16 { unsafe { std::mem::transmute(value) } }
    fn count<const N: usize>(&self, values: std::array::IntoIter<u8, N>) -> usize { values.count() }
    fn values(&self, first: u8) -> impl Iterator<Item = u8> + Clone { [first, first + 1].into_iter() }
    fn diverging(&self, call: fn() -> !) -> ! { call() }
}
pub struct Source;
impl Signatures for Source {}
