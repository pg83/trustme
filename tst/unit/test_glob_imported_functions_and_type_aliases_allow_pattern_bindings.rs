//@ aux-build: pattern_import_routes.rs
//@ compile-variant: -O
#![allow(unused_imports, non_snake_case)]
use pattern_import_routes::*;
fn main() {
    let function_name = match 479u16 {
        exported_function => exported_function + 2,
    };
    let type_name = match 487u16 {
        ExportedCount => ExportedCount + 4,
    };
    assert_eq!(function_name, 481);
    assert_eq!(type_name, 491);
    assert_eq!(pattern_import_routes::exported_function(), EXPORTED_CODE);
}
