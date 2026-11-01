fn values() -> impl Iterator<Item: Copy> + Iterator<Item: Eq> + ExactSizeIterator {
    [3u16, 5, 7].into_iter()
}

fn duplicate<T: Copy + Eq>(value: T) -> (T, T) {
    assert!(value == value);
    (value, value)
}

fn main() {
    let mut values = values();
    let pair = duplicate(values.next().unwrap());
    assert!(pair.0 == pair.1);
    assert_eq!(values.len(), 2);
}
