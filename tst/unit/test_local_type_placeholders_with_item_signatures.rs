struct Slot<T = u16>(T);
fn slot() -> Slot { Slot(7) }
fn bytes() -> [u8; { let count: _ = 3; count }] { [2, 3, 5] }
fn double(value: u16) -> u16 { value * 2 }
fn main() {
    let value: Option<_> = Some(slot().0);
    let call: fn(_) -> _ = double;
    assert_eq!(call(value.unwrap()), 14);
    let values: Vec<_> = bytes().into_iter().collect();
    assert_eq!(values, [2, 3, 5]);
}
