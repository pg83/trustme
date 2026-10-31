//@ compile-fail: Failed to find an impl of
#![feature(generic_arg_infer)]
trait Ready {}
impl Ready for [u8; 3] {}
trait Mark<const N: usize> {}
impl<const N: usize> Mark<N> for [u8; N] {}
trait Family {
    type Item<const N: usize>: Mark<N> where [u8; N]: Ready;
    fn make<const N: usize>() -> Self::Item<N> where [u8; N]: Ready;
}
struct Arrays;
impl Family for Arrays {
    type Item<const N: usize> = [u8; N] where [u8; N]: Ready;
    fn make<const N: usize>() -> Self::Item<N> where [u8; N]: Ready { [4; N] }
}
fn measured<T: Mark<7>>(_: T) {}
fn run<F: Family>() { measured(F::make::<_>()); }
fn main() { run::<Arrays>(); }
