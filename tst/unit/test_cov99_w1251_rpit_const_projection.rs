//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

pub trait Local: Sized {
    fn define<const N: usize>(bytes: [u8; N]) -> impl Copy {
        if false {
            return <Self as Local>::define(bytes);
        }
        23u8
    }
}
