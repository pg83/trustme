fn erase<T: Iterator + 'static>(values: T) -> Box<dyn Iterator<Item = T::Item>> {
    Box::new(values)
}

fn main() {
    let mut values = erase([223u16, 227u16].into_iter());
    assert_eq!(values.next(), Some(223));
    assert_eq!(values.next(), Some(227));
    assert_eq!(values.next(), None);
}
