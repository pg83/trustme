//@ proc-macro-aux-build: use_item_identity.rs
//@ aux-build: use_item_origin.rs
//@ compile-variant: -O
macro_rules! relay {
    ($item:item) => { use_item_identity::echo!($item); };
}

use_item_origin::expose!(relay);
fn main() { assert_eq!(imported(), 197); }
