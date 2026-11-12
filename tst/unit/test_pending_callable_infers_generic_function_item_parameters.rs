#![feature(unboxed_closures, fn_traits)]

struct Apply;

impl<F: FnOnce(u16) -> u16> FnOnce<(F,)> for Apply {
    type Output = u16;

    extern "rust-call" fn call_once(self, (function,): (F,)) -> u16 {
        function(419)
    }
}

fn preserve<T: Copy>(value: T) -> T { value }

fn main() {
    let apply = Apply;
    assert_eq!(apply(preserve::<_>), 419);
}
