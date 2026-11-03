//@ compile-flags: -Zmir-opt-level=0

#[inline(never)]
fn bytes() -> impl DoubleEndedIterator<Item = i8> + Clone {
    [-128, 0, 127].into_iter()
}

#[inline(never)]
fn wide() -> impl Copy + PartialEq<i128> + std::fmt::Debug {
    let value: i128 = -170141183460469231731687303715884105728;
    value
}

fn main() {
    let mut values = bytes();
    assert_eq!(values.next(), Some(i8::MIN));
    assert_eq!(values.next_back(), Some(i8::MAX));
    assert_eq!(values.clone().next(), Some(0));
    assert_eq!(wide(), i128::MIN);
}
