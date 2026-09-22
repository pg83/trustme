//@ aux-build: unsafe_fn_macro.rs
// `unsafe_code` is not reported in another crate's macro expansion
// (upstream's lint levels skip `in_external_macro` spans). An `unsafe fn`
// such a macro declares is judged by the span of the declaration, which is
// the macro's, even when the body was written by the caller: ppv-lite86's
// `dispatch_light128!` wraps rand_chacha's body in `unsafe fn fn_impl`, and
// rand_chacha is `#![forbid(unsafe_code)]`. The body's span was used and the
// crate failed to compile.
#![forbid(unsafe_code)]

unsafe_fn_macro::declare_unsafe!(seven { 7 });

fn main() {}
