//@ aux-build: pattern_import_routes.rs
fn classify(value: u16) -> bool {
    match value {
        pattern_import_routes::EXPORTED_CODE => true,
        _ => false,
    }
}
fn main() {
    assert!(classify(pattern_import_routes::source::CODE));
    assert!(!classify(469));
    assert_eq!(pattern_import_routes::exported_function(), 467);
}
