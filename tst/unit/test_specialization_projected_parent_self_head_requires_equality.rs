#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Family { type Item; }
impl Family for u8 { type Item = u16; }
struct Group<A, B, C>(PhantomData<(A, B, C)>);
trait Rank { const WIDTH: usize; }
impl<T: Family, U> Rank for Group<T, T::Item, U> { default const WIDTH: usize = 3; }
impl<T: Family> Rank for Group<T, T::Item, T::Item> { const WIDTH: usize = 5; }
type Selected = [u8; Group::<u8, u16, u16>::WIDTH];
fn main() {
    let bytes: Selected = [17; 5];
    assert_eq!(bytes, [17; 5]);
    assert_eq!(Group::<u8, u16, u32>::WIDTH, 3);
}
