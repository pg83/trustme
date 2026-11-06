#![feature(const_trait_impl)]
#[const_trait]
trait Mark<'a> {}
impl<'a> const Mark<'a> for u8 {}
trait Source { type Item; }
impl Source for () { type Item = u8; }
const fn width<T: Source<Item: for<'a> ~const Mark<'a>>>() -> usize {
    std::mem::size_of::<T::Item>()
}
const WIDTH: usize = width::<()>();
fn main() { assert_eq!(WIDTH, 1); }
