//@ edition: 2021
//@ compile-variant: -O

macro_rules! constants {
    ($( $visibility:vis $name:ident : $ty:ty = $value:expr; )*) => {
        $( $visibility const $name: $ty = $value; )*
    };
}
constants!(pub FIRST: u32 = 13; SECOND: u32 = 17;);
macro_rules! select {
    ($pattern:pat => $value:expr) => {
        match Some(5u32) { $pattern => $value, _ => 0 }
    };
}
macro_rules! alternatives {
    ($left:pat_param | $right:pat_param => $value:expr) => {
        match 1u8 { $left | $right => $value, _ => 0 }
    };
}
fn main() {
    assert_eq!(FIRST + SECOND, 30);
    assert_eq!(select!(Some(n) => n + FIRST), 18);
    assert_eq!(alternatives!(0 | 1 => 7), 7);
}
