//@ compile-variant: -O
struct Limit<const N: u16>;
impl<const N: u16> Limit<N> { const VALUE: u16 = N; }
fn classify(value: u16) -> u8 {
    match value {
        Limit::<13>::VALUE => 1,
        Limit::<17>::VALUE..=Limit::<23>::VALUE => 2,
        _ => 3,
    }
}
fn main() {
    for (value, expected) in [(0, 3), (13, 1), (16, 3), (17, 2), (23, 2), (24, 3)] {
        assert_eq!(classify(std::hint::black_box(value)), expected);
    }
}
