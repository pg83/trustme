// Extracted from library/core/src/macros/mod.rs:756
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
        // It makes no sense to `baz` a `MyStruct`, so we have no logic here
        // at all.
        // This will display "thread 'main' panicked at 'not implemented'".
        unimplemented!();
    }

    fn qux(&self) -> Result<u64, ()> {
        // We have some logic here,
        // We can add a message to unimplemented! to display our omission.
        // This will display:
        // "thread 'main' panicked at 'not implemented: MyStruct isn't quxable'".
        unimplemented!("MyStruct isn't quxable");
    }
}

fn main() {
    let s = MyStruct;
    s.bar();
}
