fn clone_capture<T: Clone>(value: T) -> T {
    let capture = move || value.clone();
    let cloned = capture.clone();
    cloned()
}

fn main() {
    assert_eq!(clone_capture(37u8), 37u8);
}
