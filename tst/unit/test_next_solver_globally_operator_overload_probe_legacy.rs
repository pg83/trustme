//@ compile-flags: -Znext-solver=globally

// The semantic-overload probe is an enumeration, not a decision: the
// canonical solver's identity response hides the overload impls it folded
// away, making `u32 / NonZero<u32>` look like a builtin-only operator and
// committing the rhs to u32.  The probe walks the legacy paths.  Mirrors
// coretests nonzero.rs (test_nonzero_uint_div/rem).

use std::num::NonZero;

fn main() {
    let nz = NonZero::new(1).unwrap();
    let x: u32 = 42u32 / nz;
    assert_eq!(x, 42u32);
    let nz10 = NonZero::new(10).unwrap();
    let y: u32 = 42u32 % nz10;
    assert_eq!(y, 2u32);
}
