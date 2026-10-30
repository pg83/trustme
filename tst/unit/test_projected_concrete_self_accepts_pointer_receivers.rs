#![feature(arbitrary_self_types_pointers)]

trait Family {
    type Item;
}

struct Value(u8);

impl Family for Value {
    type Item = Value;
}

impl Value {
    unsafe fn peek_value(self: *const <Self as Family>::Item) -> u8 {
        (*self).0
    }

    unsafe fn set_value(self: *mut <Self as Family>::Item, value: u8) {
        (*self).0 = value;
    }
}

fn main() {
    let mut value = Value(7);
    unsafe {
        assert_eq!((&raw const value).peek_value(), 7);
        (&raw mut value).set_value(13);
        assert_eq!((&raw const value).peek_value(), 13);
    }
}
