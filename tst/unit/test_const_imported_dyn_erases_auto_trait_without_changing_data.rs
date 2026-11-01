//@ aux-build: const_dyn_pointer_transforms.rs
extern crate const_dyn_pointer_transforms as source;
const VALUE: &dyn source::Value = source::remove_sync(&source::NUMBER);
fn main() { assert_eq!(VALUE.value(), 371); }
