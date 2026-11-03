//@ aux-build: orchard_object_source.rs
//@ reject-flags: --crate-object orchard_object_source=$OUT.missing.o => C Compiler failed to execute || Unrecognized option
fn main() {
    assert_eq!(orchard_object_source::folded(43), 222);
    assert_eq!(orchard_object_source::folded(u32::MAX), 2);
}
