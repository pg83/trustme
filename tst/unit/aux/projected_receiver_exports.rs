#![feature(arbitrary_self_types)]
pub trait Storage { type Pointer; }
pub trait Read: Storage<Pointer = Box<Self>> {
    fn read(self: <Self as Storage>::Pointer) -> u32;
}
