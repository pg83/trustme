//@ compile-flags: -O

trait Value {
    fn read() -> u32;
}

struct Source;

impl Value for Source {
    fn read() -> u32 {
        43
    }
}

const FUNCTION: fn() -> u32 = <Source as Value>::read;
const ADDRESS: *const () = FUNCTION as *const ();

#[inline(never)]
fn reference() -> &'static () {
    unsafe { &*ADDRESS }
}

fn main() {
    let value = reference();
    assert_eq!(*value, ());
    assert_eq!(value as *const (), ADDRESS);
    assert_eq!(FUNCTION(), 43);
}
