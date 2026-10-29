#![feature(fn_traits, unboxed_closures)]
use std::panic::AssertUnwindSafe;

struct Relay<F>(F);

impl<T, F: FnOnce() -> T> FnOnce<()> for Relay<F> {
    type Output = T;

    extern "rust-call" fn call_once(self, _: ()) -> T {
        (self.0)()
    }
}

trait Width {
    const WIDTH: usize;

    fn width(&self) -> usize {
        Self::WIDTH
    }
}

impl<T, F: FnOnce() -> Option<T>> Width for Relay<F> {
    const WIDTH: usize = std::mem::size_of::<T>();
}

fn call<F: FnOnce() -> T, T>(callback: F) -> T {
    callback()
}

fn size<F: Width>(_: &F) -> usize {
    F::WIDTH
}

fn pair() -> (u8, u16) {
    (3, 7)
}

fn main() {
    let relay = Relay(|| Some([5u16; 3]));
    assert_eq!(relay.width(), 6);
    assert_eq!(size(&relay), 6);
    assert_eq!(relay().unwrap(), [5; 3]);

    let result = call(Relay(AssertUnwindSafe(|| Ok::<_, u8>([2u16; 4]))));
    assert_eq!(result.unwrap(), [2; 4]);

    let invoke = <Relay<fn() -> (u8, u16)> as FnOnce<()>>::call_once;
    assert_eq!(invoke(Relay(pair as fn() -> (u8, u16)), ()), (3, 7));
}
