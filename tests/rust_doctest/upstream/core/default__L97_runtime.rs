// Extracted from library/core/src/default.rs:97
#![allow(unused)]
fn main() {
    #[allow(dead_code)]
    #[derive(Default)]
    struct SomeOptions {
        foo: i32,
        bar: f32,
    }
}
