#![feature(trait_alias)]
mod target {
    pub struct Data<T>(pub T);
    pub union Storage<T: Copy> { pub value: T }
    pub enum Choice<T> { One(T) }
    pub trait Tag {}
    pub fn exported(value: u8) -> u8 { value + 1 }
    pub static VALUE: u8 = 5;
}
mod aliases {
    pub use super::target::{Data, Storage, Choice, Tag, exported, VALUE};
}
type Wrapped<T> = aliases::Data<T>;
trait AnyTag = aliases::Tag;
fn accepts<T: AnyTag>(_: &T) {}
struct Container {
    data: Wrapped<u8>,
    choice: aliases::Choice<u8>,
    storage: aliases::Storage<u8>,
    function: fn(u8) -> u8,
}
std::arch::global_asm!(
    ".pushsection .data\n.quad {function}\n.quad {value}\n.popsection",
    function = sym aliases::exported,
    value = sym aliases::VALUE,
);
fn main() {
    let item = Container {
        data: aliases::Data(7),
        choice: aliases::Choice::One(11),
        storage: aliases::Storage { value: 13 },
        function: aliases::exported,
    };
    assert_eq!(item.data.0, 7);
    assert_eq!(match item.choice { aliases::Choice::One(v) => v }, 11);
    assert_eq!(unsafe { item.storage.value }, 13);
    assert_eq!((item.function)(17), 18);
    assert_eq!(aliases::VALUE, 5);
}
