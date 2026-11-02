#![feature(extern_types, ptr_metadata)]

unsafe extern "C" { type Foreign; }

#[repr(C)]
struct Leaf { tail: Foreign }

#[repr(C)]
struct Outer { inner: Leaf }

fn shared(value: &Outer) -> &Foreign { &value.inner.tail }

fn unique(value: &mut Outer) -> &mut Foreign { &mut value.inner.tail }

fn main() {
    std::hint::black_box(shared as fn(&Outer) -> &Foreign);
    std::hint::black_box(unique as fn(&mut Outer) -> &mut Foreign);
    let pointer = std::ptr::from_raw_parts::<Outer>(71usize as *const (), ());
    assert_eq!(pointer as *const () as usize, 71);
    assert_eq!(std::ptr::metadata(pointer), ());
}
