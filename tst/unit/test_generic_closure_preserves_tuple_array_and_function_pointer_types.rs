fn identity<T>(value: T) -> T {
    value
}

fn make<T: Copy>(value: T) -> impl Fn() -> (T, [T; 2], fn(T) -> T) {
    move || (value, [value; 2], identity::<T> as fn(T) -> T)
}

fn main() {
    let first = make(7u8)();
    let second = make(65537u64)();
    assert_eq!((first.0, first.1, first.2(11)), (7, [7; 2], 11));
    assert_eq!((second.0, second.1, second.2(65539)), (65537, [65537; 2], 65539));
}
