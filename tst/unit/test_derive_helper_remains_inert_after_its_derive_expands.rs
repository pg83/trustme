//@ proc-macro-aux-build: late_helper_derive.rs
trait Marked { fn number() -> u16; }
#[derive(late_helper_derive::Mark)]
#[note(accepted)]
struct Tagged;
fn main() { assert_eq!(Tagged::number(), 47); }
