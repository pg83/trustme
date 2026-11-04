#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
struct Holder<const N: usize>;
trait Left<const N: usize> where [(); N + 1]: {
    fn measure(&self, bytes: [u8; N + 1]) -> usize;
}
trait Right<const N: usize> where [(); N + 2]: {
    fn measure(&self, bytes: [u8; N + 2]) -> usize;
}
impl<const N: usize> Left<N> for Holder<N> where [(); N + 1]: {
    fn measure(&self, bytes: [u8; N + 1]) -> usize { bytes.len() + 877 }
}
impl<const N: usize> Right<N> for Holder<N> where [(); N + 2]: {
    fn measure(&self, bytes: [u8; N + 2]) -> usize { bytes.len() + 881 }
}
fn main() {
    let value = Holder::<3>;
    assert_eq!(Left::measure(&value, [13u8; 4]), 881);
    assert_eq!(Right::measure(&value, [17u8; 5]), 886);
}
