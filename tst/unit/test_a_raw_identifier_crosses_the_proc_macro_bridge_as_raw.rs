//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// thiserror's `#[error("{fn}")]` on a field `r#fn` expands to
// `let Self { r#fn } = self`, the name made with `Ident::new_raw`. Upstream's
// bridge keeps the ident raw both ways (`Ident { sym, is_raw }`), and it
// prints as `r#fn`. Our proc_macro library wrote a raw ident without its
// `r#`, so the compiler read the keyword `fn`; and an `r#fn` handed to a
// macro came in as the plain name "r#fn". The compiler in turn sent a raw
// name that is no keyword (`r#source`) without its `r#`, from tokens and from
// a struct's fields alike: thiserror takes a field `source` for the error's
// source, and `r#source` is how a struct says it is not.
use proc_macro_item_passthrough::{echo_item_spelled, raw_member, raw_name};

#[echo_item_spelled("r#source")]
struct NotSource {
    r#source: char,
}

struct S {
    r#fn: u8,
}

fn main() {
    let s = S { r#fn: 7 };
    assert_eq!(raw_member!(s), 7);
    assert_eq!(raw_name!(r#fn), "r#fn");
    assert_eq!(raw_name!(r#match), "r#match");
    assert_eq!(raw_name!(plain), "plain");
    assert_eq!(raw_name!(r#plain), "r#plain");
    assert_eq!(NotSource { r#source: 'S' }.source, 'S');
}
