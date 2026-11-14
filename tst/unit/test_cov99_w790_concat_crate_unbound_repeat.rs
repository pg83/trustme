//@ edition: 2021
//@ compile-fail: attempted to repeat an expression containing no syntax variables matched as repeating at this depth

#![feature(macro_metavar_expr_concat)]

macro_rules! concat_before_unbound_repeat {
    () => {
        fn ${concat($crate, "suffix")}() {}
        $(fn unbound() {})*
    };
}

concat_before_unbound_repeat!();

fn main() {}
