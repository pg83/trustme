//@ proc-macro-aux-build: use_item_identity.rs
//@ compile-variant: -O
macro_rules! relay {
    ($item:item) => { use_item_identity::echo!($item); };
}

mod nested {
    const VALUE: u32 = 83;
    fn value() -> u32 { 7 }
    relay!(use self::{VALUE as selected, value as chosen};);
    pub fn read() -> u32 { selected + chosen() }
}

fn main() { assert_eq!(nested::read(), 90); }
