//@ compile-flags: -Zdump-ast

fn main() {
    let mut first = 0u16;
    let mut last = 0u16;
    (first, .., last) = (3, 5, 7, 11);
    assert_eq!((first, last), (3, 11));

    [first, .., last] = [13, 17, 19, 23, 29];
    assert_eq!((first, last), (13, 29));
}
