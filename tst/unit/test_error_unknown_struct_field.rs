//@ compile-fail: error
struct S {
    a: i32,
}
fn main() {
    let _ = S { b: 1 };
}
