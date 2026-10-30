//@ aux-build: enum_alias_routes.rs
//@ compile-fail: Encountered enum at unexpected location in import
use enum_alias_routes::facade::Message::Pair::Extra;
fn main() {}
