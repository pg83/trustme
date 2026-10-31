#[derive(Debug, PartialEq)]
struct Empty();
fn invoke<F: FnOnce() -> Empty>(constructor: F) -> Empty { constructor() }
fn main() {
    let pointer: fn() -> Empty = Empty;
    assert_eq!(pointer(), Empty());
    assert_eq!(invoke(Empty), Empty());
    assert_eq!(std::mem::size_of_val(&pointer()), 0);
}
