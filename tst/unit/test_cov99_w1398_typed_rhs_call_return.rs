fn keep(value: i32) -> i32 {
    value
}

fn value() -> i32 {
    1i32 + keep(return 7)
}

fn main() {
    assert_eq!(value(), 7);
}
