//@ proc-macro-aux-build: use_item_identity.rs
//@ compile-variant: -O
macro_rules! relay {
    ($item:item) => { use_item_identity::echo!($item); };
}

relay!(use ::core::mem::{align_of as align, size_of as size};);
fn main() {
    assert_eq!(size::<[u16; 3]>(), 6);
    assert_eq!(align::<u32>(), 4);
}
