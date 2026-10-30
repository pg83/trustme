//@ aux-build: enum_alias_routes.rs
//@ compile-fail: Unable to find variant
use enum_alias_routes::facade::Message::Absent;
fn main() {}
