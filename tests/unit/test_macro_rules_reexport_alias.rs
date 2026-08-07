#[path = "support/macro_rules_reexport_alias_parent.rs"]
mod parent;

fn main() {
    assert_eq!(parent::child::value(), 43);
}
