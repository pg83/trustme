// Extracted from library/core/src/macros/mod.rs:825
#![allow(unused)]
fn main() {
    trait Foo {
        fn bar(&self) -> u8;
        fn baz(&self);
        fn qux(&self) -> Result<u64, ()>;
    }
}
