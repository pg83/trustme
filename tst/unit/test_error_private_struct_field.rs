//@ compile-fail: error
mod inner {
    pub struct S {
        value: i32,
    }
}
fn main() {
    let _ = inner::S { value: 1 };
}
