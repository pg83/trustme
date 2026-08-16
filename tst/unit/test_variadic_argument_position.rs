// `...` need not come last in a parameter list: rustc parses what follows and
// rejects it later. The parser demanded `)` straight after the marker.
//
// Same shape as the ui test parser/variadic-ffi-syntactic-pass.rs.
#[cfg(FALSE)]
extern "C" fn after_variadic(..., x: isize) {}

#[cfg(FALSE)]
fn plain_variadic(x: isize, ...) {}

unsafe extern "C" {
    fn real_variadic(x: i32, ...) -> i32;
}

fn main() {
    let _ = real_variadic;
}
