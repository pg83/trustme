#![feature(generic_assert)]

#[derive(Debug, PartialEq)]
struct Pair {
    left: u16,
    right: u16,
}

fn main() {
    let left = 17;
    let right = 29;
    assert!(Pair { left, right } == Pair { left: 17, right: 29 });
    assert_eq!((left, right), (17, 29));
}
