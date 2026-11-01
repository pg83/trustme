//@ aux-build: reexported_payload_use_targets.rs
//@ compile-fail: Variant Missing not found
type Alias = reexported_payload_use_targets::Payload;
fn main() {
    match Alias::Plain {
        Alias::Missing(_) => {},
        _ => {},
    }
}
