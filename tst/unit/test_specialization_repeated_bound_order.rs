#![feature(specialization)]
#![allow(incomplete_features)]

macro_rules! family {
    ($name:ident, $($parent:item)*) => {
        mod $name {
            trait Mark<T> {}
            trait Rank<T> { fn rank() -> u32; }
            impl<T: Mark<U> + Mark<u8> + Copy, U> Rank<U> for T {
                fn rank() -> u32 { 3 }
            }
            $($parent)*
            #[derive(Clone, Copy)]
            struct Leaf;
            impl Mark<u8> for Leaf {}
            impl Mark<u16> for Leaf {}
            pub fn check() {
                assert_eq!(<Leaf as Rank<u16>>::rank(), 3);
                assert_eq!(<Leaf as Rank<u8>>::rank(), 3);
            }
        }
    }
}

family!(root_first,
    impl<T: Mark<U>, U> Rank<U> for T {
        default fn rank() -> u32 { 1 }
    }
    impl<T: Mark<U> + Mark<u8>, U> Rank<U> for T {
        default fn rank() -> u32 { 2 }
    }
);
family!(middle_first,
    impl<T: Mark<U> + Mark<u8>, U> Rank<U> for T {
        default fn rank() -> u32 { 2 }
    }
    impl<T: Mark<U>, U> Rank<U> for T {
        default fn rank() -> u32 { 1 }
    }
);

fn main() {
    root_first::check();
    middle_first::check();
}
