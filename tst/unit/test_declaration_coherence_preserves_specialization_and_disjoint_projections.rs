#![feature(specialization)]
#![allow(incomplete_features)]

trait Project {
    type Output;
}

struct First;
struct Second;
impl Project for First {
    type Output = u8;
}
impl Project for Second {
    type Output = u16;
}

trait Permit<T> {}
impl Permit<u8> for u32 {}

trait Read<T> {
    fn read() -> u8;
}

struct Reader;
impl<T: Project> Read<T> for Reader
where
    u32: Permit<T::Output>,
{
    fn read() -> u8 {
        17
    }
}
impl Read<Second> for Reader {
    fn read() -> u8 {
        19
    }
}

struct Wrap<T>(T);
trait Choose {
    fn choose(&self) -> u8;
}
impl<T> Choose for Wrap<T> {
    default fn choose(&self) -> u8 {
        23
    }
}
impl Choose for Wrap<u8> {
    fn choose(&self) -> u8 {
        29
    }
}

fn main() {
    assert_eq!(<Reader as Read<First>>::read(), 17);
    assert_eq!(<Reader as Read<Second>>::read(), 19);
    assert_eq!(Wrap(0u16).choose(), 23);
    assert_eq!(Wrap(0u8).choose(), 29);
}
