// Extracted from library/core/src/ptr/const_ptr.rs:96
#![allow(unused)]
#![feature(set_ptr_value)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use core::fmt::Debug;
        let arr: [i32; 3] = [1, 2, 3];
        let mut ptr = arr.as_ptr() as *const dyn Debug;
        let thin = ptr as *const u8;
        unsafe {
            ptr = thin.add(8).with_metadata_of(ptr);
            assert_eq!(*(ptr as *const i32), 3);
            println!("{:?}", &*ptr); // will print "3"
        }
        Ok(())
    }
    doctest().unwrap();
}
