#![feature(unboxed_closures, fn_traits)]

struct Dispatch;

impl<F: FnOnce() -> u16> FnOnce<(F,)> for Dispatch {
    type Output = u16;

    extern "rust-call" fn call_once(self, (function,): (F,)) -> u16 {
        function() + 3
    }
}

fn source() -> u16 { 401 }

fn main() {
    assert_eq!(Dispatch(source), 404);
}
