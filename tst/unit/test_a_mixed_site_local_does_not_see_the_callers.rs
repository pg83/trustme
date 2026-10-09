//@ proc-macro-aux-build: mixed_site_local.rs
// multiversion 0.9's dispatcher binds `current_fn` at `Span::mixed_site()`
// and passes the caller's parameters, one of them also named `current_fn`.
// A mixed-site local resolves at the macro's definition, as `macro_rules!`
// locals do, so the two names are different variables. A span moved with
// `located_at` keeps its resolution: `Span::call_site().located_at(..)` is
// still the call site.
use mixed_site_local::{add_hidden, hidden_located_at_input};

fn main() {
    let value = 5u32;
    assert_eq!(add_hidden!(value), 105);
    assert_eq!(hidden_located_at_input!(x), 7);
}
