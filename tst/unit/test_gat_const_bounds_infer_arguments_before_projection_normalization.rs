#![feature(generic_arg_infer)]
trait Ready {}
impl<const N: usize> Ready for [u8; N] {}
trait Mark<const N: usize> { fn width(&self) -> usize; }
impl<const N: usize> Mark<N> for [u8; N] { fn width(&self) -> usize { N } }
trait Family {
    type Item<const N: usize>: Mark<N> where [u8; N]: Ready;
    fn make<const N: usize>() -> Self::Item<N> where [u8; N]: Ready;
}
struct Arrays;
impl Family for Arrays {
    type Item<const N: usize> = [u8; N] where [u8; N]: Ready;
    fn make<const N: usize>() -> Self::Item<N> where [u8; N]: Ready { [4; N] }
}
fn measured<T: Mark<7>>(value: T) -> usize { value.width() }
fn run<F: Family>() -> usize { measured(F::make::<_>()) }
fn main() { assert_eq!(run::<Arrays>(), 7); }
