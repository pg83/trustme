//@ compile-flags: -Zdump-ast

#[derive(PartialEq)]
struct Pair(u8, u8);

fn classify(tag: u8, pair: Pair) -> u8 {
    match tag {
        0 | 2 if pair == Pair(3, 5) => 7,
        0 | 2 => 11,
        _ => 13,
    }
}

fn main() {
    assert_eq!(classify(0, Pair(3, 5)), 7);
    assert_eq!(classify(2, Pair(3, 5)), 7);
    assert_eq!(classify(0, Pair(3, 8)), 11);
    assert_eq!(classify(2, Pair(9, 5)), 11);
    assert_eq!(classify(1, Pair(3, 5)), 13);
}
