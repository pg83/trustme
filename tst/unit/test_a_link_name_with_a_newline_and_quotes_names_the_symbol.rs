//@ aux-build: odd_symbol_aux.rs
// no-panic declares `extern "C" { #[link_name = "\n\nERROR[no-panic]: ..."] fn
// trigger() -> !; }`, and ahash's tests use it. A symbol name is any byte
// string to the object file; the C backend wrote it raw into `asm("...")`, so
// the newline ended the string literal and the C++ compile failed.
extern crate odd_symbol_aux;

extern "C" {
    #[link_name = "trustme\n\"odd\" name\t`x`\\"]
    fn declared() -> i32;
}

fn main() {
    assert_eq!(unsafe { declared() }, 7);
    assert_eq!(odd_symbol_aux::defined(), 7);
}
