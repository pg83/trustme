//@ edition: 2021

trait Select<T> {
    fn select(&self) -> u32;
}

trait Other {
    fn select(&self) -> u32;
}

trait Bundle: Select<u8> + Select<u16> + Other {}

impl Select<u8> for () {
    fn select(&self) -> u32 {
        11
    }
}

impl Select<u16> for () {
    fn select(&self) -> u32 {
        22
    }
}

impl Other for () {
    fn select(&self) -> u32 {
        33
    }
}

impl Bundle for () {}

fn main() {
    let object: &dyn Bundle = &();
    assert_eq!(Select::<u8>::select(object), 11);
    assert_eq!(Select::<u16>::select(object), 22);
    assert_eq!(Other::select(object), 33);
}
