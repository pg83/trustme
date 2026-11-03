//@ aux-build: pattern_import_routes.rs
//@ compile-fail: Couldn't find
fn main() {
    match 467u16 {
        pattern_import_routes::ExportedCount => (),
        _ => (),
    }
}
