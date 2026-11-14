//@ crate-type: lib

macro_rules! required_loop_after_optional_prefix {
    ($( $(a b)? $(c)+ )* a d) => { 796u16 };
}

pub const VALUE: u16 = required_loop_after_optional_prefix!(a d);
