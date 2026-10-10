//@ aux-build: macro_source.rs
//@ aux-build: private_macro_user.rs
//@ compile-flags: --cap-lints allow
//@ edition: 2015
// diesel_derives 2.0 (pgvector's dev-dependencies) calls `quote!` with no
// import of it: its `#[macro_use] extern crate proc_macro_error;` brings the
// private `use quote::{quote, ToTokens};` at proc-macro-error's root along.
// Upstream's `#[macro_use]` imports every macro of the crate's root, an
// inaccessible one as a compatibility entry that does not override an
// accessible one and reports the deny-by-default `private_macro_use` lint
// when used - which cargo caps for a registry dependency
// (rustc_resolve/src/build_reduced_graph.rs). We imported the exported
// macros only ("Unknown macro quote").
#[macro_use]
extern crate private_macro_user;

mod inner {
    pub fn f() -> &'static str {
        shout!()
    }
}

fn main() {
    assert_eq!(inner::f(), "loud");
    assert_eq!(private_macro_user::uses(), "loud");
}
