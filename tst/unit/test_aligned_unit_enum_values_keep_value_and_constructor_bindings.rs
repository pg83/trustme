//@ compile-variant: -O
#[repr(C, align(32))]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Aligned { Unit, Empty(), Payload(u8) }
const UNIT: Aligned = Aligned::Unit;
fn main() {
    let constructor: fn() -> Aligned = Aligned::Empty;
    assert_eq!(std::mem::align_of::<Aligned>(), 32);
    assert_eq!(std::mem::size_of::<Aligned>() % 32, 0);
    assert_eq!(constructor(), Aligned::Empty());
    assert_eq!(std::hint::black_box(UNIT), Aligned::Unit);
    let values = [UNIT, constructor(), Aligned::Payload(7)];
    let total: u32 = values.iter().map(|value| match value {
        Aligned::Unit => 1, Aligned::Empty() => 3, Aligned::Payload(value) => *value as u32,
    }).sum();
    assert_eq!(total, 11);
}
