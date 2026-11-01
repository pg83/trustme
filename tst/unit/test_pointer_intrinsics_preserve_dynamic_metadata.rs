#![feature(core_intrinsics, ptr_metadata)]

trait Read { fn read(&self) -> u16; }
impl Read for u16 { fn read(&self) -> u16 { *self } }

fn main() {
    let values = [11u16, 17, 23];
    let slice = &values[..] as *const [u16];
    assert_eq!(core::intrinsics::ptr_metadata(slice), 3);
    let value = 29u16;
    let object = &value as &dyn Read as *const dyn Read;
    let metadata = core::intrinsics::ptr_metadata(object);
    let rebuilt: *const dyn Read = core::intrinsics::aggregate_raw_ptr(object as *const (), metadata);
    assert_eq!(unsafe { (*rebuilt).read() }, 29);
    let rebuilt_slice: *const [u16] = core::intrinsics::aggregate_raw_ptr(values.as_ptr(), 2usize);
    assert_eq!(unsafe { &*rebuilt_slice }, &[11, 17]);
}
