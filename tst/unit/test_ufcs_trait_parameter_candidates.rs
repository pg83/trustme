trait Build<T> {
    fn build(value: T) -> usize;
}

struct Factory;

impl Build<u8> for Factory {
    fn build(value: u8) -> usize {
        value as usize + 100
    }
}

impl Build<u16> for Factory {
    fn build(value: u16) -> usize {
        value as usize + 200
    }
}

fn from_bound<T>(value: T) -> usize
where
    Factory: Build<T>,
{
    Factory::build(value)
}

fn main() {
    let byte: fn(u8) -> usize = Factory::build;
    let word: fn(u16) -> usize = Factory::build;
    assert_eq!(byte(3), 103);
    assert_eq!(word(5), 205);
    assert_eq!(Factory::build(7u8), 107);
    assert_eq!(Factory::build(11u16), 211);
    assert_eq!(from_bound(13u8), 113);
    assert_eq!(from_bound(17u16), 217);
}
