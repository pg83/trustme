// Extracted from library/core/src/ops/drop.rs:96
struct Foo;

impl Drop for Foo {
    fn drop(&mut self) {
        println!("Dropping Foo!")
    }
}

struct Bar;

impl Drop for Bar {
    fn drop(&mut self) {
        println!("Dropping Bar!")
    }
}

fn main() {
    let _foo = Foo;
    let _bar = Bar;
}
