//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: type annotations needed

pub trait Local: Sized {
    fn define<const N: usize>() -> impl Copy {
        let value = <Self as Local>::define();
        if false {
            return value;
        }
        let _ = value.clone();
        value
    }
}
