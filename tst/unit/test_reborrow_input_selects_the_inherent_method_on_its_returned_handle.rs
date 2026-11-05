//@ compile-variant: -O

struct Held<'a, T>(&'a T);
fn hold<T>(value: &T) -> Held<'_, T> { Held(value) }

impl<'a> Held<'a, u8> {
    fn mark(&self) -> u16 { *self.0 as u16 + 1 }
}
impl<'a> Held<'a, Box<u8>> {
    fn mark(&self) -> u16 { **self.0 as u16 + 100 }
}
impl<'a> Held<'a, Box<Box<u8>>> {
    fn mark(&self) -> u16 { ***self.0 as u16 + 200 }
}

fn main() {
    let plain = 41u8;
    let boxed = Box::new(43u8);
    let nested = Box::new(Box::new(47u8));
    assert_eq!(hold(&plain).mark(), 42);
    assert_eq!(hold(&boxed).mark(), 143);
    assert_eq!(hold(&nested).mark(), 247);
}
