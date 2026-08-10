// Extracted from src/types/closure.md:389
#![allow(unused)]
fn main() {
    let x: &mut [u8] = &mut [];
    let c = || match x { // Does not capture `*x`.
        [..] => (),
    //   ^^ Rest pattern.
    };
    let _ = &mut *x; // OK
    c();
}
