fn keep<T, const N: usize, const M: usize>(left: [T; N], right: [T; M]) -> ([T; N], [T; M]) { (left, right) }
struct Measure;
impl Measure {
    fn width<T, const N: usize>(&self, _: &[T; N]) -> usize { N }
    fn lengths<T, const N: usize, const M: usize>(&self, _: &[T; N], _: &[T; M]) -> (usize, usize) { (N, M) }
}
trait Width { fn extent<T, const N: usize>(&self, value: &[T; N]) -> usize; }
impl Width for Measure { fn extent<T, const N: usize>(&self, _: &[T; N]) -> usize { N } }
fn main() {
    let (left, right) = keep::<u16, _, _>([3, 5], [7, 11, 13]);
    assert_eq!((left, right), ([3, 5], [7, 11, 13]));
    assert_eq!(Measure.width::<u8, _>(&[17, 19, 23, 29]), 4);
    assert_eq!(Measure.extent::<u16, _>(&[31, 37]), 2);
    let (left, right) = keep::<u8, _, 3>([2, 4], [6, 8, 10]);
    assert_eq!((left, right), ([2, 4], [6, 8, 10]));
    assert_eq!(Measure.lengths::<u16, _, 3>(&[11, 13], &[17, 19, 23]), (2, 3));
}
