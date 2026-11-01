//@ aux-build: const_dyn_pointer_transforms.rs
extern crate const_dyn_pointer_transforms as source;
const VALUE: *const dyn source::Value = source::upcast_raw(&source::NUMBER);
fn main() { assert_eq!(unsafe { &*VALUE }.value(), 371); }
