// Extracted from library/core/src/default.rs:24
#[allow(dead_code)]
#[derive(Default)]
struct SomeOptions {
    foo: i32,
    bar: f32,
}

fn main() {
    let options: SomeOptions = Default::default();
}
