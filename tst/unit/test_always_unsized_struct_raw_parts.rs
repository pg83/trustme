#![feature(ptr_metadata)]

use std::ptr;

struct SliceTail {
    head: usize,
    tail: [u8],
}

struct SizedSliceTail {
    head: usize,
    tail: [u8; 3],
}

trait Value {
    fn value(&self) -> usize;
}

struct Number(usize);

impl Value for Number {
    fn value(&self) -> usize {
        self.0
    }
}

struct DynTail {
    tail: dyn Value,
}

struct SizedDynTail {
    tail: Number,
}

fn main() {
    let slice_data = SizedSliceTail { head: 42, tail: [1, 2, 3] };
    let slice_ptr = ptr::from_raw_parts::<SliceTail>(&slice_data as *const _ as *const (), 3);
    unsafe {
        assert_eq!((*slice_ptr).head, 42);
        assert_eq!(&(*slice_ptr).tail, &[1, 2, 3]);
    }

    let object: &dyn Value = &Number(0);
    let dyn_data = SizedDynTail { tail: Number(234) };
    let dyn_ptr = ptr::from_raw_parts::<DynTail>(
        &dyn_data as *const _ as *const (),
        ptr::metadata(object),
    );
    unsafe {
        assert_eq!((*dyn_ptr).tail.value(), 234);
    }
}
