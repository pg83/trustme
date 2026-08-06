// Extracted from library/core/src/mem/maybe_uninit.rs:449
#![allow(unused)]
fn main() {
    use core::pin::Pin;
    use core::mem::MaybeUninit;
    
    struct PinArena<T> {
        memory: Box<[MaybeUninit<T>]>,
        len: usize,
    }
    
    impl <T> PinArena<T> {
        pub fn capacity(&self) -> usize {
            self.memory.len()
        }
        pub fn push(&mut self, val: T) -> Pin<&mut T> {
            if self.len >= self.capacity() {
                panic!("Attempted to push to a full pin arena!");
            }
            let ref_ = self.memory[self.len].write(val);
            self.len += 1;
            unsafe { Pin::new_unchecked(ref_) }
        }
    }
}
