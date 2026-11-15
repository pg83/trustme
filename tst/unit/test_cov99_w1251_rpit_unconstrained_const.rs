//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: type annotations needed

pub trait Local: Sized {
    fn define<const N: usize>() -> impl Copy {
        <Self as Local>::define()
    }
}
