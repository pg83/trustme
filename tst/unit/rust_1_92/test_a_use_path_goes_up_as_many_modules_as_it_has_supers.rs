// `super::super::name` in a `use` goes up two modules, whatever namespace
// the name is in. mockall's tests write `use super::super::mock;` in
// `mod outer { mod inner { .. } }` for the `mock!` the crate root has
// through `use mockall::*;`.
mod macros {
    macro_rules! seven {
        () => {
            7u8
        };
    }
    pub(crate) use seven;
}

use macros::*;

mod outer {
    pub mod inner {
        use super::super::seven;

        pub fn value() -> u8 {
            seven!()
        }
    }
}

fn main() {
    assert_eq!(outer::inner::value(), 7);
}
