// proc-macro2's tests/test_size.rs pins the layout of `proc_macro`'s types:
// upstream's client holds `Span` and `TokenStream` as `NonZeroU32` handles
// into the compiler's stores and a token's text as an interned symbol, so a
// `Span` is 4 bytes (and so is `Option<Span>`), an `Ident` 12, a `Punct` 8, a
// `Literal` 16 and a `Group` 20 (its stream handle and the open, close and
// entire spans). Our library held vectors, strings and a `usize` span.
extern crate proc_macro;

use std::mem::size_of;

fn main() {
    assert_eq!(size_of::<proc_macro::Span>(), 4);
    assert_eq!(size_of::<Option<proc_macro::Span>>(), 4);
    assert_eq!(size_of::<proc_macro::Group>(), 20);
    assert_eq!(size_of::<proc_macro::Ident>(), 12);
    assert_eq!(size_of::<proc_macro::Punct>(), 8);
    assert_eq!(size_of::<proc_macro::Literal>(), 16);
    assert_eq!(size_of::<proc_macro::TokenStream>(), 4);
}
