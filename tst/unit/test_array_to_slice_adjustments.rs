fn main() {
    let mut values = [4, 1, 3, 2];

    assert_eq!(values.first(), Some(&4));
    values.sort();
    assert_eq!(&values[1..3], &[2, 3]);

    values[1..3].copy_from_slice(&[8, 9]);
    assert_eq!(values, [1, 8, 9, 4]);
}
