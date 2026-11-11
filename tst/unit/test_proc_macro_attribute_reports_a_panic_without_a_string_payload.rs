//@ proc-macro-aux-build: numeric_payload_attribute.rs
//@ compile-fail: custom attribute panicked

#[numeric_payload_attribute::stop]
fn main() {}
