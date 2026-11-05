//@ edition: 2018
//@ aux-build: qualified_namespace_route.rs
#![feature(decl_macro)]

qualified_namespace_route::home::import_value!(Imported);
qualified_namespace_route::home::import_cfg!(Predicate);

fn main() {
    assert_eq!(Imported!(), 631);
    assert_eq!(qualified_namespace_route::home::nested::value!(), 631);
    assert!(Predicate!(all()));
    assert!(!Predicate!(any()));
}
