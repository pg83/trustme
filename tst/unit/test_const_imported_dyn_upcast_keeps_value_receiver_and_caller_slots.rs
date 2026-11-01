//@ aux-build: const_dyn_pointer_transforms.rs
extern crate const_dyn_pointer_transforms as source;
const VALUE: &dyn source::Consume = source::consuming(&source::NUMBER);
fn main() { assert_eq!(VALUE.peek(), 371); }
