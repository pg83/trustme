//@ compile-fail: Both modname.rs and modname/mod.rs exist
//@ input: aux/round287_module_directory/mod.rs
//@ input: aux/round287_module_directory/leaf.rs
//@ input: aux/round287_module_directory/leaf/mod.rs
#[path = "aux/round287_module_directory/mod.rs"]
mod outer;
fn main() {}
