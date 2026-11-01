//@ compile-fail: type annotations needed
fn main() {
    let read = |&_| 1u16;
    let _ = read;
}
