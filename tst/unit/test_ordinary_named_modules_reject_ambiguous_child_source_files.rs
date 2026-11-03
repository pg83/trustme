//@ compile-fail: Both modname.rs and modname/mod.rs exist
//@ input: aux/round287_noncontrolling_directory/mod.rs
//@ input: aux/round287_noncontrolling_directory/named.rs
//@ input: aux/round287_noncontrolling_directory/named/leaf.rs
//@ input: aux/round287_noncontrolling_directory/named/leaf/mod.rs
#[path = "aux/round287_noncontrolling_directory/mod.rs"]
mod outer;
fn main() {}
