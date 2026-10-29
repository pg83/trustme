#![feature(extern_types)]

extern "C" {
    pub type Foreign;
}

pub struct Wrapped<T>(pub T);
pub enum Pick<T> {
    Full(T),
    Empty,
}
pub union Bits {
    pub word: u32,
    pub bytes: [u8; 4],
}

pub trait Base {
    type Output;
    fn produce(&self) -> Self::Output;
}
pub trait Derived: Base {
    fn bonus(&self) -> u32;
}
impl Base for Wrapped<u32> {
    type Output = Pick<Bits>;
    fn produce(&self) -> Self::Output {
        Pick::Full(Bits { word: self.0 })
    }
}
impl Derived for Wrapped<u32> {
    fn bonus(&self) -> u32 { 2 }
}

pub fn read<T: Derived<Output = Pick<Bits>>>(value: &T) -> u32 {
    let child: &dyn Derived<Output = Pick<Bits>> = value;
    let parent: &dyn Base<Output = Pick<Bits>> = child;
    match parent.produce() {
        Pick::Full(bits) => (unsafe { bits.word }) + child.bonus(),
        Pick::Empty => 0,
    }
}

pub fn deferred<T>(value: T) -> impl FnOnce() -> Pick<Wrapped<T>> {
    move || Pick::Full(Wrapped(value))
}

pub fn passthrough<T>(value: T) -> T {
    value
}
