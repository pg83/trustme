type Slot<T>=Option<T>;
type Bytes<const N: usize>=[u8; N];
fn main() {
    let slot: Slot<u16> = Some(67);
    let bytes: Bytes<3> = [7; 3];
    assert_eq!(slot, Some(67));
    assert_eq!(bytes, [7; 3]);
}
