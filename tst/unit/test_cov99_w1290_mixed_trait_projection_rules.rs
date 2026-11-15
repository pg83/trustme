//@ crate-type: lib
//@ compile-flags: --emit=metadata

pub trait WithAssoc {
    type Assoc;
}

fn mixed<T: WithAssoc + WithAssoc<Assoc = u8>>() {}

pub fn exercise<T: WithAssoc<Assoc = u8>>() {
    mixed::<T>();
    mixed::<T>();
}
