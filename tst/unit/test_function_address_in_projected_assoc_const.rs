//@ edition: 2021

trait Source<T> {
    const PTR: *const u8;
}

fn target<T>(_: &T) {}

impl<T> Source<T> for () {
    const PTR: *const u8 = target::<T> as *const u8;
}

trait Relay {
    const PTR: *const u8;
}

struct Wrapper<T>(T);

impl<T> Relay for Wrapper<T> {
    const PTR: *const u8 = <() as Source<T>>::PTR;
}

fn main() {
    let _ = <Wrapper<u8> as Relay>::PTR;
}
