// `use m::foo::{self}` names what the prefix resolved to -- the module -- and
// not a value of the same name beside it.
mod bar {
    pub mod foo {
        pub const K: u32 = 7;
    }
    pub fn foo() -> u32 {
        1
    }
}

use bar::foo::{self};

// Written without braces, the same name brings both namespaces in.
use bar::foo as both;

fn main() {
    assert_eq!(foo::K, 7);
    assert_eq!(both(), 1);
    assert_eq!(both::K, 7);
}
