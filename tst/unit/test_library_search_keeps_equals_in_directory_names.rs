//@ aux-build: literal_search_fixture.rs
//@ aux-search: literal_search_fixture.rs
//@ aux-output-dir: literal_search_fixture.rs=cache=libs
//@ compile-variant: -Lcrate=$DIR -Ldependency=$DIR -Lnative=$DIR -Lframework=$DIR -Lall=$DIR
fn main() { assert_eq!(literal_search_fixture::value(), 148); }
