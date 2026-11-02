#!trustme-probe argument [ discarded here
//@ compile-variant: -O

fn fold_digits(values: &[u8]) -> u32 {
    values.iter().fold(0, |acc, &value| acc * 10 + value as u32)
}
fn main() {
    assert_eq!(fold_digits(&[3, 1, 4]), 314);
    assert_eq!(fold_digits(&[]), 0);
}
