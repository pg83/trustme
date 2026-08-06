// Extracted from library/core/src/intrinsics/mod.rs:727
#![allow(unused)]
fn main() {
    let store = [0, 1, 2, 3];
    let v_orig = store.iter().collect::<Vec<&i32>>();
    
    // clone the vector as we will reuse them later
    let v_clone = v_orig.clone();
    
    // Using transmute: this relies on the unspecified data layout of `Vec`, which is a
    // bad idea and could cause Undefined Behavior.
    // However, it is no-copy.
    let v_transmuted = unsafe {
        std::mem::transmute::<Vec<&i32>, Vec<Option<&i32>>>(v_clone)
    };
    
    let v_clone = v_orig.clone();
    
    // This is the suggested, safe way.
    // It may copy the entire vector into a new one though, but also may not.
    let v_collected = v_clone.into_iter()
                             .map(Some)
                             .collect::<Vec<Option<&i32>>>();
    
    let v_clone = v_orig.clone();
    
    // This is the proper no-copy, unsafe way of "transmuting" a `Vec`, without relying on the
    // data layout. Instead of literally calling `transmute`, we perform a pointer cast, but
    // in terms of converting the original inner type (`&i32`) to the new one (`Option<&i32>`),
    // this has all the same caveats. Besides the information provided above, also consult the
    // [`from_raw_parts`] documentation.
    let v_from_raw = unsafe {
    
        // Ensure the original vector is not dropped.
        let mut v_clone = std::mem::ManuallyDrop::new(v_clone);
        Vec::from_raw_parts(v_clone.as_mut_ptr() as *mut Option<&i32>,
                            v_clone.len(),
                            v_clone.capacity())
    };
}
