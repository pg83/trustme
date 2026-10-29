#![feature(generic_const_exprs)]
trait Build { type Output; fn make() -> Self::Output; }
struct Bytes<const N: usize>;
impl<const N: usize> Build for Bytes<N> where [(); N + 1]: {
    type Output = [u8; N + 1];
    fn make() -> Self::Output { [7; N + 1] }
}
fn collect<T: Build<Output = [u8; M]>, const M: usize>() -> [u8; M] { T::make() }
fn grow<const N: usize>() -> [u8; N + 1] where [(); N + 1]: {
    collect::<Bytes<N>, {N + 1}>()
}
fn main() { assert_eq!(grow::<4>(), [7; 5]); }
