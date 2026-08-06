// Extracted from library/core/src/macros/mod.rs:837
trait Foo {
    fn bar(&self) -> u8;
    fn baz(&self);
    fn qux(&self) -> Result<u64, ()>;
}
struct MyStruct;

impl Foo for MyStruct {
    fn bar(&self) -> u8 {
        1 + 1
    }

    fn baz(&self) {
        // Let's not worry about implementing baz() for now
        todo!();
    }

    fn qux(&self) -> Result<u64, ()> {
        // We can add a message to todo! to display our omission.
        // This will display:
        // "thread 'main' panicked at 'not yet implemented: MyStruct is not yet quxable'".
        todo!("MyStruct is not yet quxable");
    }
}

fn main() {
    let s = MyStruct;
    s.bar();

    // We aren't even using baz() or qux(), so this is fine.
}
