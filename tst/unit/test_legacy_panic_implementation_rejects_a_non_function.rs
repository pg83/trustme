//@ compile-fail: #[panic_implementation] on non-function
#[panic_implementation]
struct Implementation;
fn main() {}
