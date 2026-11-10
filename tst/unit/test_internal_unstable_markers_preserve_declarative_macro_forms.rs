#![feature(decl_macro, allow_internal_unstable)]

#[allow_internal_unstable(core_intrinsics)]
#[macro_export]
macro bump($value:expr) {
    ($value).wrapping_add(2u16)
}

#[allow_internal_unstable(core_intrinsics)]
macro_rules! twice {
    ($value:expr) => {
        ($value).wrapping_add($value)
    };
}

fn main() {
    assert_eq!(bump!(65535u16), 1);
    assert_eq!(twice!(32769u16), 2);
}
