//@ compile-fail: #[track_caller] on non-function
struct Token;
impl Token { #[track_caller] const VALUE: u8 = 3; }
fn main() {}
