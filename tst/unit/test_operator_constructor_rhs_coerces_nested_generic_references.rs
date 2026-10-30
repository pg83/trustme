fn same<T: Ord>(left: Option<&T>, right: &&T) -> bool { left == Some(right) }
fn from_values<T: Ord>(left: &T, right: &&T) -> bool { Some(left) == Some(right) }
fn from_slice<T: Ord>(left: &[T], right: &&T) -> bool { left.first().map(|value| value) == Some(right) }
fn from_fields<T: Ord>(left: &[(u8, T)], right: &&T) -> bool { left.first().map(|value| &value.1) == Some(right) }
fn main() {
    let first = 17u16;
    let same_value = 17u16;
    let different = 23u16;
    assert!(same(Some(&first), &&same_value));
    assert!(!same(Some(&first), &&different));
    assert!(from_values(&first, &&same_value));
    assert!(!from_values(&first, &&different));
    assert!(from_slice(&[first], &&same_value));
    assert!(!from_slice(&[first], &&different));
    assert!(from_fields(&[(3, first)], &&same_value));
    assert!(!from_fields(&[(3, first)], &&different));
}
