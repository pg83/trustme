fn duplicate<I: IntoIterator<Item: Clone>>(values: I) -> Vec<I::Item> {
    let mut result = Vec::new();
    for value in values {
        result.push(value.clone());
        result.push(value);
    }
    result
}

fn sum<I: Iterator<Item = u16>>(values: I) -> u16 { values.sum() }

fn main() {
    assert_eq!(duplicate([Some(11u16), Some(17)]), [Some(11), Some(11), Some(17), Some(17)]);
    assert_eq!(sum([19u16, 23].into_iter()), 42);
}
