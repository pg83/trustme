//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

pub trait Local: Sized {
    fn define() -> impl Copy {
        if false {
            let value = <Self as Local>::define();
            value.clone()
        } else {
            23u8
        }
    }
}
