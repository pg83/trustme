//@ reject-flags: --cfg bad_index => Positional argument 2 out of range || invalid reference to positional argument 2
//@ reject-flags: --cfg star_empty => Not enough arguments passed || positional arguments in format string
//@ reject-flags: --cfg star_value => Not enough arguments passed || positional arguments in format string
//@ reject-flags: --cfg bad_precision => Unexpected character in precision || invalid format string
//@ reject-flags: --cfg bad_end => Unexpected end of formatting string || invalid format string
//@ reject-flags: --cfg bad_kind => Unknown formatting type specifier || unknown format trait
//@ reject-flags: --cfg bad_fragment => Malformed formatting fragment || invalid format string
//@ reject-flags: --cfg bad_named_precision => Unknown formatting type specifier || unknown format trait
use std::fmt;
struct Value(u16);
impl fmt::Display for Value {
    fn fmt(&self, out: &mut fmt::Formatter<'_>) -> fmt::Result { write!(out, "node({})", self.0) }
}
impl Value {
    fn render(&self) -> String { format!("{self}") }
}
#[cfg(bad_index)]
fn main() { let _ = format!("{2}", 1); }
#[cfg(star_empty)]
fn main() { let _ = format!("{:.*}"); }
#[cfg(star_value)]
fn main() { let _ = format!("{:.*}", 2usize); }
#[cfg(bad_precision)]
fn main() { let _ = format!("{:.!}", 1); }
#[cfg(bad_end)]
fn main() { let _ = format!("{:>3", 1); }
#[cfg(bad_kind)]
fn main() { let _ = format!("{:q}", 1); }
#[cfg(bad_fragment)]
fn main() { let _ = format!("{0foo}", 1); }
#[cfg(bad_named_precision)]
fn main() { let _ = format!("{:.word}", 1); }
#[cfg(not(any(bad_index, star_empty, star_value, bad_precision, bad_end, bad_kind, bad_fragment, bad_named_precision)))]
fn main() {
    assert_eq!(Value(113).render(), "node(113)");
    assert_eq!(format!("{{node}} {}", 7), "{node} 7");
    assert_eq!(format!("{:x?}", [171u16, 205]), "[ab, cd]");
    assert_eq!(format!("{:X?}", [171u16, 205]), "[AB, CD]");
    assert_eq!(format!("{:.*}", 3, "abcdef"), "abc");
}
