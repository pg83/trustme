// Extracted from library/core/src/default.rs:41
#[allow(dead_code)]
#[derive(Default)]
struct SomeOptions {
    foo: i32,
    bar: f32,
}
fn main() {
    let options = SomeOptions { foo: 42, ..Default::default() };
}
