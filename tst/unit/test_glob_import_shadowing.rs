//@ run-pass
// A glob offering a name the module already has, by its own item or by a `use`,
// loses to it -- only two globs leave a name ambiguous. And the prelude is what
// a module falls back to, so anything brought in another way shadows it.

mod m1 {
    pub struct Named(pub u8);
    pub struct OnlyHere(pub u8);
}

mod m2 {
    pub struct Named(pub u16);
}

mod shared {
    pub struct Both(pub u8);
}

mod also_shared {
    pub use super::shared::Both;
}

use m1::*;
use m2::Named;

// The same item through two globs is one item, not an ambiguity.
use also_shared::*;
use shared::*;

// The prelude has `Option`; a glob may take its place.
mod fake_prelude {
    pub struct Option(pub u8);
}
use fake_prelude::*;

fn main() {
    let named: Named = Named(7u16);
    assert_eq!(named.0, 7u16);
    assert_eq!(OnlyHere(3).0, 3u8);
    assert_eq!(Both(5).0, 5u8);
    assert_eq!(Option(9).0, 9u8);
}
