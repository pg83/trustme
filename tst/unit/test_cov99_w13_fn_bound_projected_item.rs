fn first<I: Iterator>(mut input: I) -> I::Item {
    input.next().unwrap()
}

fn require<I, T, F: FnOnce(I) -> (u8, T)>(_: F, _: I) {}

fn main() {
    require(
        first::<_>,
        [(9u8, 5u16)].into_iter()
            .filter_map(|value| value.into())
            .filter_map(|value| value.into()),
    );
}
