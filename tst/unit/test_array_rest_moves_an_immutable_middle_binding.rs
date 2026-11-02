//@ compile-variant: -O
fn main() {
    let [first, middle @ .., last] = [11u8, 13, 17, 19];
    assert_eq!((first, middle, last), (11, [13, 17], 19));
}
