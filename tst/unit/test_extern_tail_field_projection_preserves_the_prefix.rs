#![feature(extern_types)]

unsafe extern "C" { type Foreign; }

#[repr(C)]
struct Header { tag: u16, tail: Foreign }

fn prefix(value: &Header) -> u16 { value.tag }

unsafe fn tail(value: *mut Header) -> *mut Foreign {
    unsafe { std::ptr::addr_of_mut!((*value).tail) }
}

fn main() {
    std::hint::black_box(prefix as fn(&Header) -> u16);
    std::hint::black_box(tail as unsafe fn(*mut Header) -> *mut Foreign);
    assert_eq!(std::mem::size_of::<&Header>(), std::mem::size_of::<usize>());
}
