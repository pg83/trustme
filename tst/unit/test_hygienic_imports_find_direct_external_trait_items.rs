//@ aux-build: coverage_path_routes.rs
#![feature(decl_macro, import_trait_associated_functions)]
#![allow(unused_imports)]

macro imports() {
    use coverage_path_routes::api::Schema::Item as ImportedItem;
    use coverage_path_routes::api::Schema::number as imported_number;
}
imports!();

fn main() {
    assert_eq!(<coverage_path_routes::api::Runner as coverage_path_routes::api::Schema>::number(), 419);
}
