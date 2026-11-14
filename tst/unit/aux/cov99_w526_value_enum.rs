#![feature(unboxed_closures, fn_traits)]

pub enum Pulse {
    Ready,
}

impl FnOnce<()> for Pulse {
    type Output = u8;

    extern "rust-call" fn call_once(self, (): ()) -> u8 {
        53
    }
}
