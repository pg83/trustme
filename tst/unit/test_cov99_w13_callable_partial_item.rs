fn first<I: Iterator>(mut input: I) -> I::Item {
    input.next().unwrap()
}

fn main() {
    let call = first::<_>;
    let value: (u8, _) = call(
        [(9u8, 5u16)].into_iter()
            .filter_map(|value| value.into())
            .filter_map(|value| value.into()),
    );
    assert_eq!(value, (9, 5));
}
