//@ compile-fail: Failed to infer const argument
#![feature(generic_arg_infer)]
trait Mark<const N: usize> {}
struct Common;
impl<const N: usize> Mark<N> for Common {}
trait Family {
    type Item<const N: usize>: Mark<N> + Mark<7>;
    fn make<const N: usize>() -> Self::Item<N>;
}
struct Values;
impl Family for Values {
    type Item<const N: usize> = Common;
    fn make<const N: usize>() -> Self::Item<N> { Common }
}
fn measured<T: Mark<7>>(_: T) {}
fn run<F: Family>() { measured(F::make::<_>()); }
fn main() { run::<Values>(); }
