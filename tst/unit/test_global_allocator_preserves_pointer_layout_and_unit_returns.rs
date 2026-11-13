use std::alloc::{alloc, alloc_zeroed, dealloc, realloc, GlobalAlloc, Layout, System};

struct Forwarding;

unsafe impl GlobalAlloc for Forwarding {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        System.alloc(layout)
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        System.alloc_zeroed(layout)
    }

    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        System.dealloc(pointer, layout)
    }

    unsafe fn realloc(&self, pointer: *mut u8, layout: Layout, size: usize) -> *mut u8 {
        System.realloc(pointer, layout, size)
    }
}

#[global_allocator]
static ALLOCATOR: Forwarding = Forwarding;

fn main() {
    unsafe {
        let layout = Layout::from_size_align(12, 4).unwrap();
        let pointer = alloc(layout);
        assert!(!pointer.is_null());
        pointer.write(149);
        pointer.add(11).write(151);

        let grown = realloc(pointer, layout, 24);
        assert!(!grown.is_null());
        assert_eq!(grown.read(), 149);
        assert_eq!(grown.add(11).read(), 151);
        dealloc(grown, Layout::from_size_align(24, 4).unwrap());

        let zeroed = alloc_zeroed(layout);
        assert!(!zeroed.is_null());
        for index in 0..12 {
            assert_eq!(zeroed.add(index).read(), 0);
        }
        dealloc(zeroed, layout);
    }
}
