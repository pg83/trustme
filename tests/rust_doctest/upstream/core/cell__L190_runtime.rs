// Extracted from library/core/src/cell.rs:190
#![allow(unused)]
fn main() {
    use std::cell::Cell;
    use std::ptr::NonNull;
    use std::process::abort;
    use std::marker::PhantomData;

    struct Rc<T: ?Sized> {
        ptr: NonNull<RcInner<T>>,
        phantom: PhantomData<RcInner<T>>,
    }

    struct RcInner<T: ?Sized> {
        strong: Cell<usize>,
        refcount: Cell<usize>,
        value: T,
    }

    impl<T: ?Sized> Clone for Rc<T> {
        fn clone(&self) -> Rc<T> {
            self.inc_strong();
            Rc {
                ptr: self.ptr,
                phantom: PhantomData,
            }
        }
    }

    trait RcInnerPtr<T: ?Sized> {

        fn inner(&self) -> &RcInner<T>;

        fn strong(&self) -> usize {
            self.inner().strong.get()
        }

        fn inc_strong(&self) {
            self.inner()
                .strong
                .set(self.strong()
                         .checked_add(1)
                         .unwrap_or_else(|| abort() ));
        }
    }

    impl<T: ?Sized> RcInnerPtr<T> for Rc<T> {
       fn inner(&self) -> &RcInner<T> {
           unsafe {
               self.ptr.as_ref()
           }
       }
    }
}
