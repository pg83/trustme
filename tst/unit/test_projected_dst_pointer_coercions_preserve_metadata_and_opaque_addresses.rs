#![feature(ptr_metadata, extern_types)]

trait Descriptor { type Tail: ?Sized; }
struct Wide;
impl Descriptor for Wide { type Tail = [u8]; }
struct Named<D: Descriptor> { head: u32, tail: D::Tail }
struct Tuple<D: Descriptor>(u32, D::Tail);
extern "C" { type Foreign; }
fn named<D: Descriptor>(pointer: *mut Named<D>) -> *const Named<D> { pointer }
fn tuple<D: Descriptor>(pointer: *mut Tuple<D>) -> *const Tuple<D> { pointer }
fn foreign(pointer: *mut Foreign) -> *const Foreign { pointer }

fn main() {
    let slice = std::ptr::slice_from_raw_parts_mut(std::ptr::null_mut::<u8>(), 7);
    let first = named::<Wide>(slice as *mut Named<Wide>);
    let second = tuple::<Wide>(slice as *mut Tuple<Wide>);
    assert_eq!(std::ptr::metadata(first), 7);
    assert_eq!(std::ptr::metadata(second), 7);
    let opaque = foreign(1usize as *mut Foreign);
    assert_eq!(opaque as usize, 1);
}
