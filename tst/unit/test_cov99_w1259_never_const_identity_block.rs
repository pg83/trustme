//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(never_type, generic_const_exprs)]
#![allow(incomplete_features, dead_code, unused_braces)]

struct Tag<T, const N: usize>(core::marker::PhantomData<T>);

fn check<const N: usize>(value: Tag<!, N>) {
    let _: Tag<!, {{ N }}> = { value };
}

fn main() {}
