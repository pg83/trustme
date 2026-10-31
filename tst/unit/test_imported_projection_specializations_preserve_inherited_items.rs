//@ aux-build: projection_specialization_family.rs
use projection_specialization_family::{Holder, Rank};
use std::marker::PhantomData;
fn main() {
    assert_eq!(<Holder<u16> as Rank<u8, u8>>::code(), 3);
    let _: PhantomData<<Holder<u16> as Rank<u8, u8>>::Output> = PhantomData::<u64>;
}
