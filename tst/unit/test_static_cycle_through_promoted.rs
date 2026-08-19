//@ crate-type: lib
//@ compile-flags: --emit=metadata

// A static may name itself through a promoted: only its address is needed
// there, and an address is a relocation, not a value to work out again.
pub struct Value {
    pub values: &'static [&'static Value],
}

pub static VALUE: Value = Value { values: &[&VALUE] };

pub static PAIR: Value = Value {
    values: &[&VALUE, &PAIR],
};
