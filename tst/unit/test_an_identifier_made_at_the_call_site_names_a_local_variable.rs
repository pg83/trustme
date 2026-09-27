//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// paste's `[<y y>]` inside a function body pastes `yy` with the span of the
// bracket group, and it names the function's local `yy`. The group's span
// has no context of its own, so the identifier is written at the call site;
// rustc's call-site span is the context of the macro call, where the local is
// visible. We gave a call-site token no context at all, and a local declared
// in the function body was out of its sight ("Couldn't find variable name").
use proc_macro_item_passthrough::paste_idents;

fn main() {
    let yy = 5;
    paste_idents! {
        assert_eq!([<y y>], 5);
    }
}
