//@ compile-fail: error
fn main() {
    let value = 1;
    let pointer = &value as *const i32;
    let _ = *pointer;
}
