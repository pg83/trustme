//@ compile-fail: #[track_caller] on non-function
#[track_caller]
struct Token;
fn main() {}
