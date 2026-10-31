trait Encode<T, const N: usize> { type Output; fn encode(self) -> Self::Output; }
impl Encode<u8, 7> for &u16 {
    type Output = (u32, [u8; 7]);
    fn encode(self) -> Self::Output { (*self as u32 + 700, [3; 7]) }
}
fn encode<S, T, const N: usize>(values: [S; 1]) -> S::Output where S: Encode<T, N> {
    let [value] = values;
    value.encode()
}
fn main() {
    let n = 23u16;
    let values = [&n];
    let result = encode(values);
    assert_eq!(result.0, 723);
    assert_eq!(result.1, [3u8; 7]);
}
