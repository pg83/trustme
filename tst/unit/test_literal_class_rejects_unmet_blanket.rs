// A float literal ranges over the float primitives, so a trait no float
// implements cannot apply to it. `Ord` is written for every type that is a
// function pointer, and matching that impl by shape alone made `Ord::cmp` a
// candidate for the literal: the call then had two, waited, and the numeric
// default settled the receiver as f64 - where the trait the program did write
// is not implemented.

trait MyCmp {
    fn cmp(&self) -> u8 {
        32
    }
}

impl MyCmp for f32 {}

fn main() {
    assert_eq!(0.0.cmp(), 32);
}
