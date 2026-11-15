//@ crate-type: lib
//@ compile-fail: mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const NAMED: f32 = 2.0;

const fn extent<T>(value: f32) -> usize {
    core::mem::size_of::<T>() + value as usize
}

pub fn different_extents<T>(value: [u8; extent::<T>(1.0f32)]) -> [u8; extent::<T>(NAMED)]
where
    [(); extent::<T>(1.0f32)]:,
    [(); extent::<T>(NAMED)]:,
{
    value
}
