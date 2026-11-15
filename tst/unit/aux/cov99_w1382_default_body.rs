#![no_std]

pub trait Callback {
    #[inline(never)]
    fn echo(value: u32) -> u32 {
        value
    }
}

impl<T> Callback for T {}
