#[inline(never)]
fn inspect(value: [u16; 4]) -> u16 {
    match value {
        [3, middle @ .., 11] => middle[0] + middle[1],
        [first, .., last] => first + last,
    }
}
fn generic<const N: usize>(value: [u16; N]) -> usize {
    match value { _ => N }
}
fn main() {
    assert_eq!(inspect([3, 5, 7, 11]), 12);
    assert_eq!(inspect([13, 17, 19, 23]), 36);
    assert_eq!(generic([7; 33]), 33);
}
