#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Family { type Item; }
impl Family for u8 { type Item=u16; }
struct Group<A,B,C>(PhantomData<(A,B,C)>);
trait Rank { const LENGTH:usize; fn code()->u8; }
impl<T,U> Rank for Group<T,U,U> { default const LENGTH:usize=3; default fn code()->u8 {1} }
impl<T:Family> Rank for Group<T,T::Item,T::Item> { const LENGTH:usize=5; fn code()->u8 {2} }
type Selected=[u8;Group::<u8,u16,u16>::LENGTH];
fn main() { let value:Selected=[17;5]; assert_eq!(value,[17;5]); assert_eq!(Group::<u8,u32,u32>::LENGTH,3); assert_eq!(Group::<u8,u16,u16>::code(),2); }
