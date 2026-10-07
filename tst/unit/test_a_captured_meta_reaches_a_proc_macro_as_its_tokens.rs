//@ proc-macro-aux-build: dollar_crate_probe.rs
// tor-protover (arti) captures doc comments as `$(#[$meta:meta])*` and writes
// them back inside `paste! { .. }`. Upstream hands a proc macro a captured
// `meta` as the attribute's own tokens (`TokenStream::from_ast`) in an
// invisible group; we stopped at "TODO: visitToken - TOK_INTERPOLATED_...".
use dollar_crate_probe::echo;

macro_rules! with_attrs {
    ($(#[$meta:meta])* $name:ident = $value:expr) => {
        echo! { $(#[$meta])* pub const $name: u32 = $value; }
    };
}

with_attrs! { #[cfg(any())] VALUE = 1 }
with_attrs! { #[cfg(all())] #[doc = "the one that stays"] VALUE = 2 }

fn main() {
    assert_eq!(VALUE, 2);
}
