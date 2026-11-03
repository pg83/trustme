//@ proc-macro-aux-build: type_phase_identity.rs
extern crate type_phase_identity;
#[type_phase_identity::passthrough]
fn inspect<F>(callback: F, bytes: &[u8]) -> u8
where for<'a> F: Fn(&'a [u8]) -> &'a u8 {
    *callback(bytes)
}
fn first(bytes: &[u8]) -> &u8 { &bytes[0] }
fn main() { assert_eq!(inspect(first, &[37, 41, 43]), 37); }
