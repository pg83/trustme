use std::alloc::{alloc, alloc_zeroed, dealloc, realloc, Layout};

fn main() {
    unsafe {
        let layout = Layout::from_size_align(16, 8).unwrap();
        let pointer = alloc(layout);
        assert!(!pointer.is_null());
        pointer.write(137);
        pointer.add(15).write(139);

        let grown = realloc(pointer, layout, 32);
        assert!(!grown.is_null());
        assert_eq!(grown.read(), 137);
        assert_eq!(grown.add(15).read(), 139);
        dealloc(grown, Layout::from_size_align(32, 8).unwrap());

        let zeroed = alloc_zeroed(layout);
        assert!(!zeroed.is_null());
        for index in 0..16 {
            assert_eq!(zeroed.add(index).read(), 0);
        }
        dealloc(zeroed, layout);
    }
}
