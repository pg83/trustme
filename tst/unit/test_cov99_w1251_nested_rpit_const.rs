//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

pub trait Mirror {
    type Same: Copy;
}

impl<T: Copy> Mirror for T {
    type Same = T;
}

pub trait Local: Sized {
    fn define<const N: usize>(bytes: [u8; N]) -> impl Mirror<Same = impl Copy> + Copy {
        if false {
            return <Self as Local>::define(bytes);
        }
        23u8
    }
}
