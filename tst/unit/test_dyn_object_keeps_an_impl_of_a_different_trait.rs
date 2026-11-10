trait Read<T> {
    fn read(&self) -> T;
}

trait Added {
    fn added(&self) -> u16;
}

impl Added for dyn Read<u16> + '_ {
    fn added(&self) -> u16 {
        <dyn Read<u16> as Read<u16>>::read(self) + 100
    }
}

struct Source;

impl Read<u16> for Source {
    fn read(&self) -> u16 {
        41
    }
}

fn main() {
    let value: &dyn Read<u16> = &Source;
    assert_eq!(Added::added(value), 141);
    assert_eq!(<dyn Read<u16> as Read<u16>>::read(value), 41);
}
