//@ compile-fail: Type mismatch
#![feature(generic_const_exprs, type_alias_impl_trait)]
#![allow(incomplete_features)]

type A = impl Copy;
type B = impl Copy;
#[define_opaque(A)]
fn make_a() -> A { 3u8 }
#[define_opaque(B)]
fn make_b() -> B { 5u8 }

const fn width<T>() -> usize { std::mem::size_of::<T>() }

fn change<T>(value: [u8; width::<(T, A)>()]) -> [u8; width::<(T, B)>()]
where
    [(); width::<(T, A)>()]:,
    [(); width::<(T, B)>()]:,
{
    value
}

fn main() {}
