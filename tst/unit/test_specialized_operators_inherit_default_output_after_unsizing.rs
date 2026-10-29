#![feature(specialization, coerce_unsized, unsize)]
#![allow(incomplete_features)]
use std::ops::{Add, CoerceUnsized, Neg};
use std::marker::Unsize;
struct Ref<'a, T: ?Sized>(&'a T);
impl<T: ?Sized> Copy for Ref<'_, T> {}
impl<T: ?Sized> Clone for Ref<'_, T> { fn clone(&self) -> Self { Ref(self.0) } }
impl<'a, T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Ref<'a, U>> for Ref<'a, T> {}
impl<T: ?Sized> Neg for Ref<'_, T> {
    default type Output = usize;
    default fn neg(self) -> Self::Output { panic!("unused default") }
}
impl Neg for Ref<'_, [u8]> { fn neg(self) -> usize { self.0.len() + 10 } }
struct Extra<const N: usize>;
impl<T: ?Sized, const N: usize> Add<Extra<N>> for Ref<'_, T> {
    default type Output = usize;
    default fn add(self, _: Extra<N>) -> Self::Output { panic!("unused default") }
}
impl Add<Extra<7>> for Ref<'_, [u8]> {
    fn add(self, _: Extra<7>) -> usize { self.0.len() + 7 }
}
fn main() {
    let a = [1u8, 2];
    let b = [3u8, 4, 5];
    let c = [6u8, 7, 8, 9];
    let result = -[Ref::<[u8]>(&a[..]), Ref::<[u8; 3]>(&b), Ref::<[u8; 4]>(&c)][0];
    assert_eq!(result, 12);
    let sum = [Ref::<[u8]>(&a[..]), Ref::<[u8; 3]>(&b), Ref::<[u8; 4]>(&c)][0] + Extra::<7>;
    assert_eq!(sum, 9);
}
