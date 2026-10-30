#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]
trait Route<T:?Sized,const N:usize> { fn carry(value:[u16;N]) -> [u16;N]; }
struct All;
impl<T:?Sized,const N:usize> Route<T,N> for All { fn carry(value:[u16;N]) -> [u16;N] { value } }
fn check<F,const N:usize>(value:[u16;N]) -> [u16;N] where for<T> F: Route<*const T,N> { <F as Route<*const str,N>>::carry(<F as Route<*const u16,N>>::carry(value)) }
fn check_inner<F,const N:usize>(value:[u16;N]) -> [u16;N] where F: for<T> Route<*const T,N> { <F as Route<*const u8,N>>::carry(value) }
fn main() { assert_eq!(check_inner::<All,2>([17,19]),[17,19]); assert_eq!(check::<All,3>([7,11,13]),[7,11,13]); assert_eq!(check::<All,0>([]),[]); }
