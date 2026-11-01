//@ proc-macro-aux-build: late_module_attributes.rs
macro_rules! install { () => { use late_module_attributes::require_source as LateAttribute; }; }
#[LateAttribute]
trait Value {
    fn value() -> u16 { #[derive(Clone)] struct Local(u16); Local(31).clone().0 }
}
struct Shell;
impl Value for Shell {}
install!();
fn main() { assert_eq!(Shell::value(), 31); }
