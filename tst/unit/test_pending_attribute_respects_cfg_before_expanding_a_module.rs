//@ proc-macro-aux-build: late_module_attributes.rs
macro_rules! install { () => { use late_module_attributes::require_source as LateAttribute; }; }
#[LateAttribute]
#[cfg(any())]
mod discarded { compile_error!("discarded module expanded"); }
install!();
fn main() { assert_eq!(17u16 + 2, 19); }
