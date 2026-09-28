//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
//@ aux-build: crate_helper_host.rs
// unic-langid's `langid!` (fluent-bundle, fluent-langneg, unic-langid-macros)
// is proc-macro-hack's: a `macro_rules!` of the declaring crate expands to
// `#[derive($crate::..hack)] enum ProcMacroHack {..} proc_macro_call!()`, and the
// derive writes `macro_rules! proc_macro_call { () => { $crate::subtags::.. } }`
// at its call site. Upstream `$crate` names the crate of the outermost
// non-transparent expansion in the token's context; a call-site token has the
// derive's call site as its context, which is inside `langid!`, so `$crate` is
// the declaring crate. It was taken as the crate being compiled whenever the
// token came out of a proc macro (batch 4's `made_crate_macro` case, where the
// call site is the user's own crate, stays the same).
use crate_helper_host::helper_via_derive;

fn main() {
    assert_eq!(helper_via_derive!(), 9);
}
