//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// pest_derive (handlebars' grammar, through winnow's dev-dependencies) hands
// back a `Punct` with `Spacing::Joint` followed by a group. Joint only says
// the next token follows without a space; rustc's bridge ends an operator at
// the first token that is not a `Punct`. Our proc_macro library panicked with
// "Punct(Joint) not followed by another Punct".
use proc_macro_item_passthrough::joint_punct_before_group;

joint_punct_before_group!();

fn main() {
    assert_eq!(joint_made(), 5);
    assert_eq!(JOINT_END, 6);
}
