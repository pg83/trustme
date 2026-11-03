//@ aux-build: pattern_import_routes.rs
//@ compile-fail: pointing to unexpected item type - Function
fn main() {
    match 467u16 {
        pattern_import_routes::exported_function => (),
        _ => (),
    }
}
