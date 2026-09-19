// Upstream hands a proc macro the annotated item's own recorded tokens, each keeping the
// context it was written in (`TokenStream::from_ast`, rustc_ast/src/tokenstream.rs); no
// token there picks up a context from the token before it. We have no recorded tokens and
// re-print the item instead, and on our wire a context marker holds until the next one -
// so a name printed out of the AST, which sent no marker of its own, took the context of
// whatever went out last. An attribute on a parameter is passed through as real tokens and
// left its own context in force, so the parameter's name went out in it while the body
// printed after it went out at the call site, and the two stopped naming the same binding.
// base64's rstest cases hit this through `#[values(..)]` on a template parameter.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item;

#[echo_item]
fn twice(#[allow(unused_mut)] value: u32) -> u32 {
    value * 2
}

#[echo_item]
fn longest<'a>(#[allow(unused_mut)] first: &'a str, second: &'a str) -> &'a str {
    if first.len() >= second.len() {
        first
    } else {
        second
    }
}

fn main() {
    assert_eq!(twice(3), 6);
    assert_eq!(longest("abcd", "xy"), "abcd");
}
