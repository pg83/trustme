//@ compile-fail: ambigu
macro_rules! sizes {
    ($( $($name:ident)? ),* ; done) => { [$( { $(let _ = stringify!($name);)? 1u16 } ),*] };
}
fn main() { let _: [u16; 0] = sizes!(; done); }
