const fn value() -> i32 {
    const { 5 }
}

fn main() {
    let value: &'static i32 = &const { value() };
    assert_eq!(*value, 5);
}
