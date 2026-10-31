//@ compile-fail: type annotations needed
#![feature(generic_arg_infer)]
trait Mark<const N: usize> {}
impl<const N: usize> Mark<N> for [u8; N] {}
trait Family {
    type Item<const N: usize>;
    fn make<const N: usize>() -> Self::Item<N>;
}
struct Arrays;
impl Family for Arrays {
    type Item<const N: usize> = [u8; N];
    fn make<const N: usize>() -> Self::Item<N> { [4; N] }
}
fn measured<T: Mark<7>>(_: T) {}
fn run<F: Family>() { measured(F::make::<_>()); }
fn main() { run::<Arrays>(); }
