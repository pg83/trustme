//@ aux-build: root_path_target.rs
//@ proc-macro-aux-build: root_path_2015_macro.rs
// rocket_codegen 0.5 (edition 2021) derives `FromMeta` with devise_codegen,
// an edition-2015 macro whose output says `use ::devise::ext::..;`. A `::`
// path written by a 2015 macro in a crate of a later edition resolves in the
// crate root and then in the extern prelude (rustc's
// `CrateRootAndExternPrelude`), in a `use` as anywhere else.
use root_path_2015_macro::root_path_call;

fn main() {
    assert_eq!(root_path_call!(), 14);
}
