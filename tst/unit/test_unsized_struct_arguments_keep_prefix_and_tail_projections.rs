#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

#[repr(C)]
struct Holder<T: ?Sized> { tag: u16, body: T }

trait Read { fn read(&self) -> u16; }
impl Read for u16 { fn read(&self) -> u16 { *self } }

#[inline(never)]
fn slice(value: Holder<[u16]>) -> u16 { value.tag + value.body[1] }

#[inline(never)]
fn dynamic(value: Holder<dyn Read>) -> u16 { value.tag + value.body.read() }

fn main() {
    let values: Box<Holder<[u16]>> = Box::new(Holder { tag: 7, body: [11, 13] });
    let object: Box<Holder<dyn Read>> = Box::new(Holder { tag: 17, body: 19u16 });
    assert_eq!(slice(*values), 20);
    assert_eq!(dynamic(*object), 36);
}
