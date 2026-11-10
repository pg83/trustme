//@ compile-flags: -O -Zdump-mir

trait Base {
    fn read(&self) -> u32;
}

trait Middle: Base {
    fn middle(&self) -> u32;
}

trait Top: Middle {
    fn top(&self) -> u32;
}

struct Source(u32);

impl Base for Source {
    fn read(&self) -> u32 { self.0 }
}

impl Middle for Source {
    fn middle(&self) -> u32 { self.0 + 2 }
}

impl Top for Source {
    fn top(&self) -> u32 { self.0 + 4 }
}

static SOURCE: Source = Source(67);
const OBJECT: &dyn Top = &SOURCE;

#[inline(never)]
fn read() -> u32 {
    let middle: &dyn Middle = OBJECT;
    let base: &dyn Base = middle;
    base.read()
}

fn main() {
    assert_eq!(read(), 67);
    assert_eq!(OBJECT.middle(), 69);
    assert_eq!(OBJECT.top(), 71);
}
