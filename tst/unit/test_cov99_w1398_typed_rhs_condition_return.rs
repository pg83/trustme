fn value() -> i32 {
    1i32 + if (return 7) { 2i32 } else { 3i32 }
}

fn main() {
    assert_eq!(value(), 7);
}
