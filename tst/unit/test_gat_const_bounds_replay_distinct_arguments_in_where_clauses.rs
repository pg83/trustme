#![feature(generic_arg_infer)]
trait Ready {}
impl<const N: usize, const M: usize> Ready for ([u8; N], [u8; M]) {}
trait Mark<const A: usize, const B: usize> { fn widths(&self) -> (usize, usize); }
impl<const N: usize, const M: usize> Mark<M, N> for ([u8; N], [u8; M]) {
    fn widths(&self) -> (usize, usize) { (M, N) }
}
trait Family {
    type Item<const N: usize, const M: usize>: Mark<M, N> where ([u8; N], [u8; M]): Ready;
    fn make<const N: usize, const M: usize>() -> Self::Item<N, M> where ([u8; N], [u8; M]): Ready;
}
struct Arrays;
impl Family for Arrays {
    type Item<const N: usize, const M: usize> = ([u8; N], [u8; M]) where ([u8; N], [u8; M]): Ready;
    fn make<const N: usize, const M: usize>() -> Self::Item<N, M> where ([u8; N], [u8; M]): Ready { ([4; N], [5; M]) }
}
fn measured<T: Mark<3, 7>>(value: T) -> (usize, usize) { value.widths() }
fn run<F: Family>() -> (usize, usize) { measured(F::make::<_, _>()) }
fn main() { assert_eq!(run::<Arrays>(), (3, 7)); }
