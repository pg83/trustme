//@ aux-build: const_dyn_pointer_transforms.rs
extern crate const_dyn_pointer_transforms as source;
fn bump(value: u16) -> u16 { value + 19 }
const CALLBACK: unsafe fn(u16) -> u16 = source::weaken_callback(bump);
fn main() { assert_eq!(unsafe { CALLBACK(31) }, 50); }
