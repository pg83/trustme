// Extracted from library/core/src/pin.rs:704
#![allow(unused)]
fn main() {
    struct Field;
    struct Struct {
        field: Field,
        // ...
    }

    impl Struct {
        fn field(&mut self) -> &mut Field { &mut self.field }
    }
}
