//@ compile-fail: error
fn borrow_local() -> &'static i32 {
    let value = 1;
    &value
}
fn main() {}
