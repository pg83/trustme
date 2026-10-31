//@ compile-fail: Type mismatch
#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Rank { type Output; }
impl<T> Rank for T { default type Output = u16; }
impl Rank for u8 { type Output = u32; }
fn main() { let _: PhantomData<<u8 as Rank>::Output> = PhantomData::<u16>; }
