#![feature(fn_delegation)]
#![allow(incomplete_features)]
trait Convert: Sized {
    fn converted<I: Iterator<Item: Into<Self>>>(items: I) -> Vec<Self> {
        items.map(Into::into).collect()
    }
    fn count<const N: usize>(items: std::array::IntoIter<Self, N>) -> usize {
        items.count()
    }
}
impl Convert for u32 {}
reuse Convert::{converted, count};
fn main() {
    let values: Vec<u32> = converted([2u8, 3, 5].into_iter());
    assert_eq!(values, [2, 3, 5]);
    assert_eq!(count([7u32, 11].into_iter()), 2);
}
