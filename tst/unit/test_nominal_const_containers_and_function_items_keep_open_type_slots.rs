struct Envelope<T, const N: usize>([Option<T>; N]);
fn pass<T, const N: usize>(value: [T; N]) -> [T; N] { value }
fn main() {
    let mut envelope = Envelope([None, None, None]);
    envelope.0[1] = Some(331u32);
    assert_eq!(envelope.0, [None, Some(331), None]);
    let callback = pass::<_, 3>;
    assert_eq!(callback([337u16, 347, 349]), [337, 347, 349]);
}
