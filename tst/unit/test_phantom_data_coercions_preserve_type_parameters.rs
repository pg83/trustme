//@ compile-fail: Type mismatch
use std::marker::PhantomData;
fn main() { let _: PhantomData<u16> = PhantomData::<u32>; }
