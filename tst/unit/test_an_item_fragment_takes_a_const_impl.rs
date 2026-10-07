#![feature(const_trait_impl)]

macro_rules! documented {
    (#[$meta:meta] $item:item) => {
        #[$meta]
        $item
    };
}

pub const trait Zero {
    fn zero() -> Self;
}

documented! {
    #[doc = "zero"]
    #[allow(unused_attributes)]
    impl const Zero for u8 {
        fn zero() -> u8 {
            0
        }
    }
}

pub struct Wrapper<T>(T);

documented! {
    #[doc = "wrapped"]
    impl<T: [const] Zero> const Zero for Wrapper<T>
    where
        T: [const] Zero,
    {
        fn zero() -> Self {
            Wrapper(T::zero())
        }
    }
}

const Z: Wrapper<u8> = <Wrapper<u8> as Zero>::zero();

fn main() {
    assert_eq!(Z.0, 0);
    assert_eq!(<u8 as Zero>::zero(), 0);
}
