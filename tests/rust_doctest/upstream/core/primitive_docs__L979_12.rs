// Extracted from library/core/src/primitive_docs.rs:979
#![allow(unused)]
fn main() {
    use std::slice;
    use std::str;
    
    let story = "Once upon a time...";
    
    let ptr = story.as_ptr();
    let len = story.len();
    
    // story has nineteen bytes
    assert_eq!(19, len);
    
    // We can re-build a str out of ptr and len. This is all unsafe because
    // we are responsible for making sure the two components are valid:
    let s = unsafe {
        // First, we build a &[u8]...
        let slice = slice::from_raw_parts(ptr, len);
    
        // ... and then convert that slice into a string slice
        str::from_utf8(slice)
    };
    
    assert_eq!(s, Ok(story));
}
