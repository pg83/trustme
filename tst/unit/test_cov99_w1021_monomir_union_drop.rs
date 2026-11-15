//@ crate-type: lib
//@ compile-flags: -C codegen-type=monomir
//@ output-contains: .mir DROP ZR
#![no_std]

pub union UnionWithDrop {
    pub value: u32,
}

impl Drop for UnionWithDrop {
    fn drop(&mut self) {}
}

pub fn consume(value: u32) {
    let _value = UnionWithDrop { value };
}
