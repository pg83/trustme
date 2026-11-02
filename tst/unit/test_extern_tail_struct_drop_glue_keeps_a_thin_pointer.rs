#![feature(extern_types)]

unsafe extern "C" { type Foreign; }

#[repr(C)]
struct Header { tag: u16, tail: Foreign }

impl Drop for Header {
    fn drop(&mut self) { std::hint::black_box(self.tag); }
}

unsafe fn destroy(value: *mut Header) {
    unsafe { std::ptr::drop_in_place(value); }
}

fn main() {
    std::hint::black_box(destroy as unsafe fn(*mut Header));
    assert_eq!(std::mem::size_of::<*mut Header>(), std::mem::size_of::<usize>());
}
