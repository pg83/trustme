// Extracted from library/core/src/marker.rs:1295
#![allow(unused)]
#![feature(derive_coerce_pointee)]
fn main() {
    use std::marker::CoercePointee;
    use std::ops::Deref;
    use std::ptr::NonNull;

    #[derive(CoercePointee)]
    #[repr(transparent)]
    pub struct Rc<T: ?Sized> {
        inner: NonNull<RcInner<T>>,
    }

    struct RcInner<T: ?Sized> {
        refcount: usize,
        value: T,
    }

    impl<T: ?Sized> Deref for Rc<T> {
        type Target = T;
        fn deref(&self) -> &T {
            let ptr = self.inner.as_ptr();
            unsafe { &(*ptr).value }
        }
    }

    impl<T> Rc<T> {
        pub fn new(value: T) -> Self {
            let inner = Box::new(RcInner {
                refcount: 1,
                value,
            });
            Self {
                inner: NonNull::from(Box::leak(inner)),
            }
        }
    }

    impl<T: ?Sized> Clone for Rc<T> {
        fn clone(&self) -> Self {
            // A real implementation would handle overflow here.
            unsafe { (*self.inner.as_ptr()).refcount += 1 };
            Self { inner: self.inner }
        }
    }

    impl<T: ?Sized> Drop for Rc<T> {
        fn drop(&mut self) {
            let ptr = self.inner.as_ptr();
            unsafe { (*ptr).refcount -= 1 };
            if unsafe { (*ptr).refcount } == 0 {
                drop(unsafe { Box::from_raw(ptr) });
            }
        }
    }
}
