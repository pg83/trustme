// Extracted from src/paths.md:112
#![allow(unused)]
fn main() {
    fn f<const N: usize>() -> [u8; N] { [0; _] }
    let _: [_; 1] = f::<{ _ }>();
    //                    ^ ERROR `_` not allowed here
}
