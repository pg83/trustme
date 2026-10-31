pub trait Family { type Item; }
impl Family for u8 { type Item = u32; }
struct Holder<X>(std::marker::PhantomData<X>);
impl<X> Holder<X> {
    #[inline(never)]
    fn value<Y: Copy>() -> u32 where X: Family<Item = u32> { 37 }
}
#[inline(never)]
fn step(value: u32) -> u32 { value + 7 }
#[inline(never)]
fn apply<F: Fn(u32) -> u32>(callback: F) -> u32 { callback(41) }
#[inline(never)]
fn count<const N: usize>() -> usize where [(); N]: Copy { N }
pub fn mixed<T: Clone>(value: T) -> (T, u32, u32, usize) {
    (value.clone(), Holder::<u8>::value::<u16>(), apply(step as fn(u32) -> u32), count::<3>())
}
