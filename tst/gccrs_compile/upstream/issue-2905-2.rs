use std::marker::PhantomData;

pub struct Weird<'a, T> {
    ptr: *const T,
    end: *const T,
    marker: PhantomData<&'a T>,
}

impl<'a, T> Weird<'a, T> {
    pub fn new(slice: &'a [T]) -> Self {
        let ptr = slice.as_ptr();
        let end = unsafe { ptr.add(slice.len()) };
        Self { ptr, end, marker: PhantomData }
    }
}

impl<'a, T> Iterator for Weird<'a, T> {
    type Item = &'a T;
    fn next(&mut self) -> Option<&'a T> {
        if self.ptr == self.end { return None; }
        let current = self.ptr;
        self.ptr = unsafe { self.ptr.add(1) };
        Some(unsafe { &*current })
    }
}

fn main() { let values=[1,2,3]; let _ = Weird::new(&values).next(); }
