fn identity<T>(value: T) -> T {
    value
}

fn main() {
    assert_eq!(identity(17), 17);
}
