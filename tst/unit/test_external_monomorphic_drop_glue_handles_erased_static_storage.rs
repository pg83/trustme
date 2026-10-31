//@ aux-build: external_drop_values.rs
extern crate external_drop_values;
use external_drop_values::{recorded, storage, Value};

fn main() {
    let object = storage() as *mut dyn Value;
    unsafe {
        assert_eq!((*object).value(), 17);
        std::ptr::drop_in_place(object);
    }
    assert_eq!(recorded(), 17);
}
