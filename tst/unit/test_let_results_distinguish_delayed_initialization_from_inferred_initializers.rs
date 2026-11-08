fn identity<T>(value: T) -> T {
    value
}

fn main() {
    let delayed;
    if std::hint::black_box(true) {
        delayed = 17u16;
    } else {
        delayed = 19;
    }

    let inferred = identity(Default::default());
    let (typed,): (u16,) = (inferred,);
    assert_eq!((delayed, typed), (17, 0));
}
