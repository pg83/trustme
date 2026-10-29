#![feature(specialization)]
trait Ready {}
impl Ready for u8 {}
struct Grid<T, const N: usize, const M: usize>([[T; N]; M]);
trait Rank { fn rank(&self) -> u8; }
impl<T: Ready, const N: usize, const M: usize> Rank for Grid<T, N, M> {
    default fn rank(&self) -> u8 { 1 }
}
impl<T: Ready, const N: usize> Rank for Grid<T, N, N> {
    fn rank(&self) -> u8 { 2 }
}
fn main() {
    assert_eq!(Grid([[5u8; 2]; 3]).rank(), 1);
    assert_eq!(Grid([[5u8; 2]; 2]).rank(), 2);
}
