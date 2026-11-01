//@ proc-macro-aux-build: late_module_attributes.rs
macro_rules! install { () => { use late_module_attributes::require_source as LateAttribute; }; }
struct Shell;
impl Shell {
    #[LateAttribute]
    fn value() -> u16 { #[derive(Clone)] struct Local(u16); Local(29).clone().0 }
}
install!();
fn main() { assert_eq!(Shell::value(), 29); }
