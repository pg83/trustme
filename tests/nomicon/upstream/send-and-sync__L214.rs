// Extracted from src/send-and-sync.md:214
#![allow(unused)]
fn main() {
    struct Carton<T>(std::ptr::NonNull<T>);
    unsafe impl<T> Send for Carton<T> where Box<T>: Send {}
}
