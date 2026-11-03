//@ compile-fail: Can't find file
//@ input: aux/round287_named_directory/root.rs
//@ input: aux/round287_named_directory/root/leaf.rs
//@ input: aux/round287_named_directory/root/leaf/mod.rs
#[path = "aux/round287_named_directory/root.rs"]
mod outer;
fn main() {}
