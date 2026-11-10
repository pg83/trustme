trait Amount {
    const BASE: u16;
    fn amount(&self) -> u16;
}

struct Root(u16);

impl Root {
    const fn new(value: u16) -> Self { Self(value) }
}

impl Amount for Root {
    const BASE: u16 = 41;
    fn amount(&self) -> u16 { Self::BASE + self.0 }
}

const FIRST: Root = Root::new(13);

fn main() {
    assert_eq!(FIRST.amount(), 54);
    assert_eq!(Root::new(19).amount(), 60);
    assert_eq!(<Root as Amount>::BASE, 41);
}
