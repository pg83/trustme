#[repr(C)]
struct Holder<T: ?Sized> { tag: u16, body: T }

trait Read { fn read(&self) -> u16; }
impl Read for u16 { fn read(&self) -> u16 { *self } }

#[inline(never)]
fn slice(value: &Holder<[u16]>) -> u16 { value.tag + value.body[1] }

#[inline(never)]
fn dynamic(value: &Holder<dyn Read>) -> u16 { value.tag + value.body.read() }

#[inline(never)]
fn sized(value: &Holder<(u16, [u16; 2])>) -> u16 {
    value.tag + value.body.0 + value.body.1[1]
}

fn main() {
    assert_eq!(slice(&Holder { tag: 3, body: [5, 7] }), 10);
    assert_eq!(dynamic(&Holder { tag: 11, body: 13u16 }), 24);
    let owned = Box::new(Holder { tag: 17, body: (19, [23, 29]) });
    assert_eq!(sized(&owned), 65);
    assert_eq!(owned.body.1[0], 23);
}
