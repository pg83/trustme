//@ edition: 2021

#![allow(static_mut_refs)]

static mut LOG: u32 = 0;

struct Field;

impl Drop for Field {
    fn drop(&mut self) {
        unsafe { LOG = LOG * 10 + 1 };
    }
}

enum Value {
    Present(Field),
}

impl Drop for Value {
    fn drop(&mut self) {
        unsafe { LOG = LOG * 10 + 2 };
    }
}

fn main() {
    {
        let _value = Value::Present(Field);
    }
    assert_eq!(unsafe { LOG }, 21);
}
