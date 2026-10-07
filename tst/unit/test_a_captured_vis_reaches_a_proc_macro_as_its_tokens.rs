//@ proc-macro-aux-build: dollar_crate_probe.rs
// tor-basic-utils' `n_key_list!` (arti) captures the list's visibility as
// `$vis:vis` and writes it back inside a proc macro's input. Upstream hands a
// proc macro a captured `vis` as the visibility's own tokens in an invisible
// group, and an empty one as an empty group; we stopped at "TODO: visitToken -
// TOK_INTERPOLATED_...".
mod inner {
    macro_rules! constant {
        ($v:vis $name:ident = $value:expr) => {
            dollar_crate_probe::echo! { $v const $name: u32 = $value; }
        };
    }

    constant! { pub(crate) SHARED = 5 }
    constant! { HIDDEN = 6 }

    pub fn hidden() -> u32 {
        HIDDEN
    }
}

fn main() {
    assert_eq!(inner::SHARED, 5);
    assert_eq!(inner::hidden(), 6);
}
