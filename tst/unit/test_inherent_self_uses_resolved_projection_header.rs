#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Family { type Item; }
impl Family for u8 { type Item=u16; }
struct Group<A,B,C>(PhantomData<(A,B,C)>);
impl<T,U> Group<T,U,U> { const BASE:usize=7; }
impl<T:Family> Group<T,T::Item,T::Item> {
 fn array()->[u8;Self::BASE] where [();Self::BASE]: { [3;Self::BASE] }
}
fn main() { assert_eq!(Group::<u8,u16,u16>::array(),[3;7]); }
