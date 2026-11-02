//@ compile-variant: -O
enum Digit { One = 1, Three = 3 }
const fn width(value: Digit) -> usize { value as usize }
fn sum<const N: usize>(bytes: [u8; N]) -> u32 { bytes.into_iter().map(u32::from).sum() }
const WIDTH: usize = width(Digit::Three);
fn main() {
    assert_eq!(sum::<{ Digit::One as usize }>([13]), 13);
    assert_eq!(sum::<WIDTH>([2, 5, 9]), 16);
    assert_eq!(width(Digit::One), 1);
}
