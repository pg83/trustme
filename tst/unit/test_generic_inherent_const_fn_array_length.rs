#![feature(adt_const_params)]
#![allow(incomplete_features)]

mod inner {
    struct Wrapper<T>(T);

    impl<T> Wrapper<T> {
        const fn value() -> usize {
            3
        }
    }

    struct Array<const N: [u8; Wrapper::<u32>::value()]>;
}

fn main() {}
