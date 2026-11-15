//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

fn zeros<const M: usize>() -> [u8; M] {
    [0; M]
}

pub trait Local: Sized {
    fn define<const N: usize>(_: [u8; N]) -> impl Copy {
        let recurse = <Self as Local>::define;
        if false {
            return recurse(zeros());
        }
        let _: fn([u8; N]) -> _ = recurse;
        23u8
    }
}
