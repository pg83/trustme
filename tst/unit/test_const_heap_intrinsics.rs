#![feature(const_heap, core_intrinsics, ptr_metadata)]

use std::intrinsics::{const_allocate, const_deallocate, const_make_global};
use std::mem::{align_of, size_of};

#[repr(align(1024))]
struct Aligned(u8);

trait HeapTrait {
    fn value(&self) -> u32 {
        99
    }
}

struct HeapZst;

impl HeapTrait for HeapZst {}

const TRANSIENT: u32 = unsafe {
    let ptr = const_allocate(size_of::<u32>(), align_of::<u32>()) as *mut u32;
    ptr.write(42);
    let value = ptr.read();
    const_deallocate(ptr.cast(), size_of::<u32>(), align_of::<u32>());
    value
};

const GLOBAL: &Aligned = unsafe {
    let ptr = const_allocate(size_of::<Aligned>(), align_of::<Aligned>()) as *mut Aligned;
    ptr.write(Aligned(7));
    let ptr = const_make_global(ptr.cast()) as *const Aligned;
    &*ptr
};

const ODD_ALIGNED_GLOBAL: &[u8; 3] = unsafe {
    let ptr = const_allocate(3, 1024) as *mut [u8; 3];
    ptr.write([11, 22, 33]);
    let ptr = const_make_global(ptr.cast()) as *const [u8; 3];
    &*ptr
};

const DEALLOCATING_GLOBAL_IS_A_NOP: () = unsafe {
    const_deallocate(
        (GLOBAL as *const Aligned).cast_mut().cast(),
        size_of::<Aligned>(),
        align_of::<Aligned>(),
    );
};

const GLOBAL_VTABLE: &std::ptr::DynMetadata<dyn HeapTrait> = unsafe {
    let ptr = const_allocate(
        size_of::<std::ptr::DynMetadata<dyn HeapTrait>>(),
        align_of::<std::ptr::DynMetadata<dyn HeapTrait>>(),
    ) as *mut std::ptr::DynMetadata<dyn HeapTrait>;
    let object = std::ptr::NonNull::<HeapZst>::dangling().as_ptr() as *const dyn HeapTrait;
    ptr.write(std::ptr::metadata(object));
    let ptr = const_make_global(ptr.cast()) as *const std::ptr::DynMetadata<dyn HeapTrait>;
    &*ptr
};

fn main() {
    assert_eq!(TRANSIENT, 42);
    assert_eq!(GLOBAL.0, 7);
    assert_eq!((GLOBAL as *const Aligned).addr() % align_of::<Aligned>(), 0);
    assert_eq!(*ODD_ALIGNED_GLOBAL, [11, 22, 33]);
    assert_eq!(ODD_ALIGNED_GLOBAL.as_ptr().addr() % 1024, 0);

    let data = std::ptr::NonNull::<HeapZst>::dangling().as_ptr().cast::<()>();
    let object = std::ptr::from_raw_parts::<dyn HeapTrait>(data, *GLOBAL_VTABLE);
    assert_eq!(unsafe { (&*object).value() }, 99);
}
