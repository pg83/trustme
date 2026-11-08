struct Receiver(u32);

impl Receiver {
    fn by_value(#[cfg(any())] self, value: u32) -> u32 { value + 1 }
    fn by_reference(#[cfg_attr(all(), cfg(any()))] &self, value: u32) -> u32 { value + 2 }
    fn explicit(#[cfg(any())] self: Box<Self>, value: u32) -> u32 { value + 3 }
    fn kept(#[cfg(all())] &self, value: u32) -> u32 { self.0 + value }
}

fn main() {
    assert_eq!(Receiver::by_value(40), 41);
    assert_eq!(Receiver::by_reference(40), 42);
    assert_eq!(Receiver::explicit(40), 43);
    assert_eq!(Receiver(7).kept(5), 12);
}
