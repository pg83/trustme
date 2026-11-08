//@ aux-build: module_alias_type_endpoint.rs
//@ compile-fail: Unexpected item type returned for
type Wrong = module_alias_type_endpoint::Facade;
fn main() {}
