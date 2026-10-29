//@ compile-flags: -C opt-level=2
#[inline(never)]
fn checksum(value: ((u32, u32), (u32, u32))) -> u32 {
    value.0.0 + value.0.1 + value.1.0 + value.1.1
}
#[inline(never)]
fn unchanged(first: (u32, u32), second: (u32, u32)) -> (u32, u32, u32) {
    let pair = (first, second);
    let sum = checksum(pair);
    (pair.0.1, pair.1.0, sum)
}
#[inline(never)]
fn mutated(mut first: (u32, u32), second: (u32, u32)) -> (u32, u32, u32) {
    let pair = (first, second);
    first.1 = 41;
    let sum = checksum(pair);
    (pair.0.1, first.1, sum)
}
fn main() {
    let first = std::hint::black_box((2, 3));
    let second = std::hint::black_box((5, 7));
    assert_eq!(unchanged(first, second), (3, 5, 17));
    assert_eq!(mutated(first, second), (3, 41, 17));
}
