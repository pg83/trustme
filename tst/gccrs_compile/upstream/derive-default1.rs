#[derive(Default)]
struct Foo {
    _a: i32,
    _b: i64,
    _c: u8,
}

fn main() {
    let _ = Foo::default();
}
