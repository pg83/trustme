//@ check-pass
//@ compile-flags: -Znext-solver

#![feature(rustc_attrs)]

#[rustc_coinductive]
trait Trait<T> {}

impl<'a, 'b, T> Trait<T> for (&'a (), &'b ())
where
    'b: 'a,
    &'a (): Trait<T>,
{
}

impl Trait<i32> for &'static () {}

impl<'a> Trait<u32> for &'a ()
where
    for<'b> (&'a (), &'b ()): Trait<u32>,
{
}

fn impls_trait<T: Trait<U>, U>() {}

fn main() {
    impls_trait::<(&(), &()), _>();
}
