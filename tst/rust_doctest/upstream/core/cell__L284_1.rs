// Extracted from library/core/src/cell.rs:284
#![allow(unused)]
fn main() {
    use std::cell::Cell;

    struct SomeStruct {
        regular_field: u8,
        special_field: Cell<u8>,
    }

    let my_struct = SomeStruct {
        regular_field: 0,
        special_field: Cell::new(1),
    };

    let new_value = 100;

    // ERROR: `my_struct` is immutable
    // my_struct.regular_field = new_value;

    // WORKS: although `my_struct` is immutable, `special_field` is a `Cell`,
    // which can always be mutated
    my_struct.special_field.set(new_value);
    assert_eq!(my_struct.special_field.get(), new_value);
}
