// Extracted from library/core/src/ptr/mut_ptr.rs:79
#![allow(unused)]
#![feature(set_ptr_value)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use core::fmt::Debug;
        let mut arr: [i32; 3] = [1, 2, 3];
        let mut ptr = arr.as_mut_ptr() as *mut dyn Debug;
        let thin = ptr as *mut u8;
        unsafe {
            ptr = thin.add(8).with_metadata_of(ptr);
            assert_eq!(*(ptr as *mut i32), 3);
            println!("{:?}", &*ptr); // will print "3"
        }
        Ok(())
    }
    doctest().unwrap();
}
