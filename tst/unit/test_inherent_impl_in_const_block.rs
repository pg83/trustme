macro_rules! cfg_32 {
    ($($tokens:tt)+) => {
        #[cfg(target_pointer_width = "32")]
        $($tokens)+
    };
}

macro_rules! cfg_64 {
    ($($tokens:tt)+) => {
        #[cfg(target_pointer_width = "64")]
        $($tokens)+
    };
}

macro_rules! define_impl {
    ($on_32:item $on_64:item) => {
        cfg_32!($on_32);
        cfg_64!($on_64);
    };
}

mod parent {
    pub mod iter {
        define_impl!(
            pub struct Digits<'a> {
                pub(super) data: &'a [u32],
            }
            pub struct Digits<'a> {
                pub(super) data: &'a [u64],
            }
        );

        define_impl!(
            const _: () = {
                impl<'a> Digits<'a> {
                    pub(super) fn new(data: &'a [u32]) -> Self {
                        Digits { data }
                    }
                }
            };
            const _: () = {
                impl<'a> Digits<'a> {
                    pub(super) fn new(data: &'a [u64]) -> Self {
                        Digits { data }
                    }
                }
            };
        );
    }

    pub fn first(data: &[u64]) -> u64 {
        iter::Digits::new(data).data[0]
    }
}

fn main() {
    assert_eq!(parent::first(&[1]), 1);
}
