fn main() {
    let mut left = Default::default();
    let mut right = Default::default();
    left = 1 + right;
    right = left + 1;
    assert_eq!((left, right), (1, 2));
}
