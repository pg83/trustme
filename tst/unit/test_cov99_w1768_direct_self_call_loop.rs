//@ crate-type: lib
//@ compile-flags: --emit=metadata -Z mir-opt-level=2 -Z dump-mir

pub fn a_repeat() {
    loop {
        a_repeat();
    }
}

pub fn z_entry() {
    a_repeat();
}
