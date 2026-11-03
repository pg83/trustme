fn main() {
    let mut values: Box<dyn Iterator<Item = _> + Send> = Box::new([271u32, 277].into_iter());
    assert_eq!(values.next(), Some(271));
    assert_eq!(values.next(), Some(277));
    assert_eq!(values.next(), None);
    let mut borrowed: &mut (dyn Iterator<Item = _> + Send) = &mut std::iter::once(281u16);
    assert_eq!(borrowed.next(), Some(281));
}
