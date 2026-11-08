//@ compile-fail: #[track_caller] on non-function
trait Traits { #[track_caller] const VALUE: u8; }
fn main() {}
