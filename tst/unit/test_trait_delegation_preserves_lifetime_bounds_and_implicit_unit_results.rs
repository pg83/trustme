#![feature(fn_delegation)]
#![allow(incomplete_features)]

trait Read<'a> {
    fn read(&self) -> &'a u16;
}

struct Source<'a>(&'a u16);

impl<'a> Read<'a> for Source<'a> {
    fn read(&self) -> &'a u16 { self.0 }
}

trait Receive {
    fn set(&mut self, value: u16);

    fn capture<'a, T: Read<'a>>(&mut self, source: T) {
        self.set(*source.read());
    }
}

impl Receive for u16 {
    fn set(&mut self, value: u16) { *self = value; }
}

reuse Receive::capture;

fn main() {
    let first = 83u16;
    let second = 89u16;
    let mut target = 0u16;
    assert_eq!(capture(&mut target, Source(&first)), ());
    assert_eq!(target, 83);
    assert_eq!(capture(&mut target, Source(&second)), ());
    assert_eq!(target, 89);
}
