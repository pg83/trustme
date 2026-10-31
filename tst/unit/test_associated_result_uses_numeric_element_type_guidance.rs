trait Encode {
    type Output;
    fn encode(self) -> Self::Output;
}
impl Encode for u32 { type Output = u64; fn encode(self) -> u64 { self as u64 + 100 } }
fn encode<T: Encode<Output = u64>>(values: [T; 2]) -> [u64; 2] {
    values.map(T::encode)
}
fn main() {
    let values = [Default::default(), 17u32];
    assert_eq!(encode(values), [100u64, 117]);
}
