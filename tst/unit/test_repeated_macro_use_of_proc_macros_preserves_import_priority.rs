//@ proc-macro-aux-build: macro_import_value_first.rs
//@ proc-macro-aux-build: macro_import_value_second.rs
//@ compile-variant: -O

#[macro_use]
extern crate macro_import_value_first as first;
#[macro_use]
extern crate macro_import_value_first as again;
#[macro_use]
extern crate macro_import_value_second as second;

fn main() {
    assert_eq!(parcel!(), 67);
    assert_eq!(first_only!(), 13);
    assert_eq!(first::parcel!(), 61);
    assert_eq!(again::parcel!(), 61);
    assert_eq!(second::parcel!(), 67);
}
