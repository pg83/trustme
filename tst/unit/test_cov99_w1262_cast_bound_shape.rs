//@ crate-type: lib
//@ compile-flags: -Cpanic=abort
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn require_sized<T: Sized>() {}

pub fn compare_bound_shapes<const N: usize>()
where
    [(); N + 1]: Sized,
    [(); N as usize]: Sized,
{
    require_sized::<[(); N as usize]>();
}
