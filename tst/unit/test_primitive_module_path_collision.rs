//@ run-pass

// `std::*` imports the external `std::str` module into the type namespace.
// Generated formatting support still has to resolve its unqualified `str`
// types to the primitive.
use std::*;

fn main() {
    let _ = format!("rayon");
    assert_eq!(str::to_owned("rayon"), String::from("rayon"));
}
