#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Rank { type Output; }
impl<T> Rank for T { default type Output = u16; }
impl Rank for u8 {}
fn require<T: Rank<Output = u16>>() -> PhantomData<T::Output> { PhantomData::<u16> }
fn main() { let _: PhantomData<u16> = require::<u8>(); }
