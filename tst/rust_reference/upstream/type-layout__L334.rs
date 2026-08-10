// Extracted from src/type-layout.md:334
#![allow(unused)]
fn main() {
    #[repr(C)]
    union Union {
        f1: u16,
        f2: [u8; 4],
    }
    
    assert_eq!(std::mem::size_of::<Union>(), 4);  // From f2
    assert_eq!(std::mem::align_of::<Union>(), 2); // From f1
    
    assert_eq!(std::mem::offset_of!(Union, f1), 0);
    assert_eq!(std::mem::offset_of!(Union, f2), 0);
    
    #[repr(C)]
    union SizeRoundedUp {
       a: u32,
       b: [u16; 3],
    }
    
    assert_eq!(std::mem::size_of::<SizeRoundedUp>(), 8);  // Size of 6 from b,
                                                          // rounded up to 8 from
                                                          // alignment of a.
    assert_eq!(std::mem::align_of::<SizeRoundedUp>(), 4); // From a
    
    assert_eq!(std::mem::offset_of!(SizeRoundedUp, a), 0);
    assert_eq!(std::mem::offset_of!(SizeRoundedUp, b), 0);
}
