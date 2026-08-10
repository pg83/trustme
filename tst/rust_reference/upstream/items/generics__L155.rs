// Extracted from src/items/generics.md:155
#![allow(unused)]
fn main() {
    fn make_buf<const N: usize>() -> [u8; N] {
        [0; _]
        //  ^ Infers `N`.
    }
    let _: [u8; 1024] = make_buf::<_>();
    //                             ^ Infers `1024`.
}
