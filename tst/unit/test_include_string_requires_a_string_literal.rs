//@ compile-fail: include! requires a string literal

fn main() {
    let _ = include_str!(true);
}
