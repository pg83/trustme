//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// proc-macro-hack's `#[proc_macro_hack] pub use impl_crate::hex;` expands to
// a `macro_rules! hex` whose body derives through `$crate::_proc_macro_hack_hex`,
// every token written by the proc macro at its call site. rustc resolves
// `$crate` to the crate that defines the `macro_rules!` (the macro's
// definition scope); we resolved it to the proc macro's own crate and found
// no `_proc_macro_hack_hex` there (sha2's hex-literal dev-dependency).
use proc_macro_item_passthrough::made_crate_macro;

fn helper() -> u8 {
    7
}

made_crate_macro!();

fn main() {
    assert_eq!(call_helper!(), 7);
}
