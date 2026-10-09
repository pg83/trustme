//@ aux-build: dollar_crate_ident.rs
// typewit 1.15 matches `$crate::m!(..)` with `$($macro:ident)::* !(..)`.
// `$crate` from another crate's macro is one identifier token: it matches
// `ident`, and `$crate :: inner` is three token trees.
use dollar_crate_ident::{crate_path_tts, outer};

fn main() {
    assert_eq!(outer!(2), 3);
    assert_eq!(crate_path_tts!(), 2);
}
