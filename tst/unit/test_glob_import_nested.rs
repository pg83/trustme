//@ run-pass
// Only the globs a module writes itself decide whether a name it offers is
// ambiguous. A glob of that module reaches its own globs too, but what the
// module says under a name has already shadowed those there, so the name
// arrives settled.

mod b {
    pub mod inner {
        pub struct Named(pub u8);
    }

    pub use self::inner::*;

    pub struct Named(pub u16);
}

use crate::b::*;

fn main() {
    let x: crate::b::Named = Named(1);
    assert_eq!(x.0, 1u16);
}
