//@ compile-fail: requires a string literal
fn main() {
    let _ = format_args!(true);
}
