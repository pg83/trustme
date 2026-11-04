//@ aux-build: external_pattern_namespaces.rs
//@ compile-fail: error

fn main() {
    match 97u16 {
        external_pattern_namespaces::PatternMarker => (),
        _ => (),
    }
}
