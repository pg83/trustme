// `#[rustc_macro_transparency = "transparent"]` leaves the names the expansion
// declares in the caller's scope, so the caller's tokens can use them. That is
// what lets `mir!` declare `RET` for the caller's tokens to assign to.
#![feature(decl_macro, rustc_attrs)]
#![allow(internal_features, non_snake_case)]

#[rustc_macro_transparency = "transparent"]
macro declare_and_run($body:expr) {{
    let VALUE = 7;
    $body
}}

macro opaque_declare_and_run($body:expr) {{
    let VALUE = 7;
    let _ = VALUE;
    $body
}}

fn main() {
    assert_eq!(declare_and_run!(VALUE * 2), 14);

    // Without the attribute the macro's own name is not the caller's.
    let VALUE = 1;
    assert_eq!(opaque_declare_and_run!(VALUE * 2), 2);
}
