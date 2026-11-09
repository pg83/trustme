//@ compile-fail: Duplicate definition of named argument
fn main() {
    let _ = format_args!("{entry}", entry = 103u8, entry = 107u8);
}
