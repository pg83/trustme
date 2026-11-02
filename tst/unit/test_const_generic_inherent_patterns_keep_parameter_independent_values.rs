//@ compile-variant: -O
struct Limit<const N: usize>;
impl<const N: usize> Limit<N> { const VALUE: u8 = 13; }
fn matches<const N: usize>(value: u8) -> bool {
    match value { Limit::<N>::VALUE => true, _ => false }
}
fn main() {
    let _ = matches::<3>(13);
    for value in [0, 13, 17] {
        assert_eq!(matches::<3>(std::hint::black_box(value)), value == 13);
        assert_eq!(matches::<9>(std::hint::black_box(value)), value == 13);
    }
}
