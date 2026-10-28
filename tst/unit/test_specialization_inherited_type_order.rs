#![feature(specialization)]
#![allow(incomplete_features)]

macro_rules! family {
    ($name:ident, $($parent:item)*) => {
        mod $name {
            trait Mark { type Item; }
            trait Info { type Value; fn rank() -> u32; }
            impl<T: Mark<Item = u8> + Copy> Info for T {
                fn rank() -> u32 { 3 }
            }
            $($parent)*
            #[derive(Clone, Copy)]
            struct Leaf;
            impl Mark for Leaf { type Item = u8; }
            pub fn check() {
                let value: <Leaf as Info>::Value = 7u8;
                assert_eq!(value, 7);
                assert_eq!(Leaf::rank(), 3);
            }
        }
    }
}

family!(middle_first,
    impl<T: Mark<Item = u8>> Info for T {
        default type Value = u8;
        default fn rank() -> u32 { 2 }
    }
    impl<T: Mark> Info for T {
        default type Value = u16;
        default fn rank() -> u32 { 1 }
    }
);
family!(root_first,
    impl<T: Mark> Info for T {
        default type Value = u16;
        default fn rank() -> u32 { 1 }
    }
    impl<T: Mark<Item = u8>> Info for T {
        default type Value = u8;
        default fn rank() -> u32 { 2 }
    }
);

fn main() {
    middle_first::check();
    root_first::check();
}
