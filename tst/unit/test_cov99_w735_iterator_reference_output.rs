fn first<I: Iterator>(mut source: I) -> I::Item {
    source.next().unwrap()
}

fn identity<T>(value: T) -> T {
    value
}

fn main() {
    let value = 41u8;
    let result = *first((0..1).map(|_| identity(&value)));
    assert_eq!(result, 41);
}
