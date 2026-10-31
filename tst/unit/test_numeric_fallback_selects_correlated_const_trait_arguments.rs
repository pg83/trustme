#![feature(generic_arg_infer)]
trait Shape<const N: usize, const M: usize, T> {
    fn shape(value: T) -> ([u8; N], [u8; M]);
}
struct Arrays;
impl Shape<3, 3, i32> for Arrays {
    fn shape(value: i32) -> ([u8; 3], [u8; 3]) { ([value as u8; 3], [9; 3]) }
}
impl Shape<5, 5, i64> for Arrays {
    fn shape(value: i64) -> ([u8; 5], [u8; 5]) { ([value as u8; 5], [8; 5]) }
}
fn main() {
    let (left, right) = <Arrays as Shape<_, _, _>>::shape(6);
    assert_eq!(left.len(), 3);
    assert_eq!(right.len(), left.len());
    assert_eq!(left[2], 6);
    assert_eq!(right[2], 9);
}
