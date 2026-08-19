//@ compile-fail: is ambiguous
// Two globs offering one name shadow nothing, so the name is an error where it
// is used -- not where it was brought in. Bringing both in is fine on its own.

mod m1 {
    pub struct Ambig;
}

mod m2 {
    pub struct Ambig;
}

use m1::*;
use m2::*;

fn main() {
    let _ = Ambig;
}
