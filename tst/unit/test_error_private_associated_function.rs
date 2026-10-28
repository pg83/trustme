//@ compile-fail: Failed to find impl with 'value'
mod inner {
    pub struct S;
    impl S {
        fn value() -> i32 { 1 }
    }
}
fn main() {
    let _ = inner::S::value();
}
