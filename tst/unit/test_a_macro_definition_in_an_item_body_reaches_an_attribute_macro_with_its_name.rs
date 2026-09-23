// async-trait moves a function's body into an `async move` block, and its
// tests define `macro_rules! t { .. }` inside such a body. Upstream prints a
// macro definition with its name (`print_mac_def`: `macro_rules! t { .. }`);
// our printer dropped the name and the reparse failed with `macro_rules!
// requires an identifier`.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item_spelled;

#[echo_item_spelled("! t {")]
fn with_local_macro() -> u8 {
    macro_rules! t {
        () => {
            7
        };
    }
    t!()
}

fn main() {
    assert_eq!(with_local_macro(), 7);
}
