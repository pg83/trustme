fn first<I: Iterator>(mut iterator: I) -> Option<I::Item> {
    iterator.next()
}

fn main() {
    let iterator = [0u8, 1].into_iter().filter_map(|number| {
        if number == 0 { None } else { Some(101) }
    });
    let unresolved = first(iterator);
    let value: Option<u16> = unresolved;
    assert_eq!(value, Some(101));
}
