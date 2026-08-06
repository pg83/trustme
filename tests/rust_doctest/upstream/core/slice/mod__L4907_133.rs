// Extracted from library/core/src/slice/mod.rs:4907
#![allow(unused)]
#![feature(align_to_uninit_mut)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::mem::MaybeUninit;
        
        pub struct BumpAllocator<'scope> {
            memory: &'scope mut [MaybeUninit<u8>],
        }
        
        impl<'scope> BumpAllocator<'scope> {
            pub fn new(memory: &'scope mut [MaybeUninit<u8>]) -> Self {
                Self { memory }
            }
            pub fn try_alloc_uninit<T>(&mut self) -> Option<&'scope mut MaybeUninit<T>> {
                let first_end = self.memory.as_ptr().align_offset(align_of::<T>()) + size_of::<T>();
                let prefix = self.memory.split_off_mut(..first_end)?;
                Some(&mut prefix.align_to_uninit_mut::<T>().1[0])
            }
            pub fn try_alloc_u32(&mut self, value: u32) -> Option<&'scope mut u32> {
                let uninit = self.try_alloc_uninit()?;
                Some(uninit.write(value))
            }
        }
        
        let mut memory = [MaybeUninit::<u8>::uninit(); 10];
        let mut allocator = BumpAllocator::new(&mut memory);
        let v = allocator.try_alloc_u32(42);
        assert_eq!(v, Some(&mut 42));
        Ok(())
    }
    doctest().unwrap();
}
