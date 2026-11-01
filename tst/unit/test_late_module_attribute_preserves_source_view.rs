//@ proc-macro-aux-build: late_module_attributes.rs
macro_rules! install { () => { use late_module_attributes::require_source as LateAttribute; }; }
#[LateAttribute]
mod model {
    #[derive(Clone, PartialEq)]
    pub struct Wrap<T>(pub T);
}
install!();
fn main() {
    let original = model::Wrap(17u16);
    assert!(original.clone() == original);
    assert!(model::Wrap(19u16) != original);
}
