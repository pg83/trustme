//@ proc-macro-aux-build: late_module_attributes.rs
macro_rules! install { () => { use late_module_attributes::require_source as LateAttribute; }; }
#[LateAttribute]
fn value() -> u16 {
    #[derive(Clone)] struct Local(u16);
    Local(23).clone().0
}
install!();
fn main() { assert_eq!(value(), 23); }
