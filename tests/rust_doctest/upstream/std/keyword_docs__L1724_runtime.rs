// Extracted from library/std/src/keyword_docs.rs:1724
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        fn debug_iter<I: Iterator>(it: I) where I::Item: std::fmt::Debug {
            for elem in it {
                println!("{elem:#?}");
            }
        }
        
        // u8_len_1, u8_len_2 and u8_len_3 are equivalent
        
        fn u8_len_1(val: impl Into<Vec<u8>>) -> usize {
            val.into().len()
        }
        
        fn u8_len_2<T: Into<Vec<u8>>>(val: T) -> usize {
            val.into().len()
        }
        
        fn u8_len_3<T>(val: T) -> usize
        where
            T: Into<Vec<u8>>,
        {
            val.into().len()
        }
        Ok(())
    }
    doctest().unwrap();
}
