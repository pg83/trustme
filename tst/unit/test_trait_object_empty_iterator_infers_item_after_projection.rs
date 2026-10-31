fn main() {
    let mut empty = std::iter::empty();
    let object: &mut dyn Iterator<Item = _> = &mut empty;
    let missing = object.next();
    let mut values = [missing.unwrap_or(29u32), 31u32];
    values.reverse();
    assert_eq!(values, [31, 29]);
    assert_eq!(object.next(), None);
}
