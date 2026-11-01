//@ aux-build: const_dyn_pointer_transforms.rs
extern crate const_dyn_pointer_transforms as source;
const VALUE: &dyn source::Associated<u8, 7, Output = u32> = source::associated(&source::NUMBER);
fn main() { assert_eq!(VALUE.apply(9), 387); }
