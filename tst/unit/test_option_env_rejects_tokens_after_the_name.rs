//@ compile-fail: Unexpected token after string literal
fn main() {
    let _ = option_env!("unused-name", true);
}
