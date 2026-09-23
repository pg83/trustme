//@ aux-build: modal_parser_aux.rs
// `(opt(b"ab"), ws()).void()` with `opt` from another crate: the `void`
// probe sets `O` to `(Option<<?I as Stream>::Slice>, ())` before it learns
// `?I` from `ws()`. Upstream's `resolve_vars_if_possible` resolves a
// variable's value all the way down, so the method's `O` names the input
// type; a resolution that stopped at the first variable left the probe's
// `?I` in the selected path, and nothing ever decided it. toml_edit's
// `document` parser on winnow 0.7.
use modal_parser_aux::*;
fn ws<'i>() -> impl Modal<In<'i>, (), Ctx> {
    move |_i: &mut In<'i>| Ok(())
}
fn document<'i>() -> impl Modal<In<'i>, (), Ctx> {
    move |i: &mut In<'i>| (opt(b"ab"), ws()).void().parse_next(i)
}
fn main() {
    let mut i: In<'_> = Wrap(Wrap(Raw(b"abc"), ()), ());
    assert!(document().parse_next(&mut i).is_ok());
}
