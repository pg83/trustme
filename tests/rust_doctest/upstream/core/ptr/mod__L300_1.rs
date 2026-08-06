// Extracted from library/core/src/ptr/mod.rs:300
#![allow(unused)]
fn main() {
    unsafe {
        // A flag we want to pack into our pointer
        static HAS_DATA: usize = 0x1;
        static FLAG_MASK: usize = !HAS_DATA;
    
        // Our value, which must have enough alignment to have spare least-significant-bits.
        let my_precious_data: u32 = 17;
        assert!(align_of::<u32>() > 1);
    
        // Create a tagged pointer
        let ptr = &my_precious_data as *const u32;
        let tagged = ptr.map_addr(|addr| addr | HAS_DATA);
    
        // Check the flag:
        if tagged.addr() & HAS_DATA != 0 {
            // Untag and read the pointer
            let data = *tagged.map_addr(|addr| addr & FLAG_MASK);
            assert_eq!(data, 17);
        } else {
            unreachable!()
        }
    }
}
