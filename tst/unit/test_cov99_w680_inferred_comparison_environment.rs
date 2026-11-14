fn at_least_default<T: PartialOrd<u8>>(left: &T) -> bool {
    let right = Default::default();
    left >= &right
}

fn main() {
    assert!(at_least_default(&5u8));
}
