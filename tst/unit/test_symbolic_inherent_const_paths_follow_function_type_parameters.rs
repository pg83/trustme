#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
struct Size<T>(PhantomData<T>);
impl<T> Size<T> { const WIDTH: usize = std::mem::size_of::<T>(); }
fn forward<T>(value: [u8; Size::<T>::WIDTH]) -> [u8; Size::<T>::WIDTH] { value }
struct Route;
impl Route {
    fn relay<Unused, T>(value: [u8; Size::<T>::WIDTH]) -> [u8; Size::<T>::WIDTH] { forward::<T>(value) }
}
fn main() {
    assert_eq!(Route::relay::<u64, u16>([7;2]), [7;2]);
    assert_eq!(Route::relay::<u8, u32>([9;4]), [9;4]);
}
