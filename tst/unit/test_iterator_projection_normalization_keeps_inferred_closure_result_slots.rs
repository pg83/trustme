fn first<I: Iterator>(mut iterator: I) -> I::Item {
    iterator.next().unwrap()
}

fn main() {
    let values = [false, true].into_iter().map(|flag| {
        if flag { 97 } else { 89 }
    });
    let unresolved = first(values);
    let value: u16 = unresolved;
    assert_eq!(value, 89);
}
