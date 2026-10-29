#![feature(specialization)]
trait Ready {}
impl Ready for [u8; 2] {}
impl Ready for [u8; 3] {}
trait Rank { fn rank(&self) -> u8; }
impl Rank for ([u8; 3], [u8; 3]) { fn rank(&self) -> u8 { 3 } }
impl<const N: usize, const M: usize> Rank for ([u8; N], [u8; M])
where [u8; N]: Ready, [u8; M]: Ready {
    default fn rank(&self) -> u8 { 1 }
}
impl<const N: usize> Rank for ([u8; N], [u8; N]) where [u8; N]: Ready {
    default fn rank(&self) -> u8 { 2 }
}
fn main() {
    assert_eq!(([0; 2], [0; 3]).rank(), 1);
    assert_eq!(([0; 2], [0; 2]).rank(), 2);
    assert_eq!(([0; 3], [0; 3]).rank(), 3);
}
