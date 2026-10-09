pub trait DerefVTable {
    const VTABLE: unsafe fn(*const *const ()) -> *const ();
}

unsafe fn no_deref(ptr: *const *const ()) -> *const () {
    ptr as *const ()
}

unsafe fn deref(ptr: *const *const ()) -> *const () {
    unsafe { *ptr }
}

pub struct ByVal<T>(pub T);

impl<T: Copy> DerefVTable for ByVal<T> {
    const VTABLE: unsafe fn(*const *const ()) -> *const () = {
        if core::mem::size_of::<T>() <= core::mem::size_of::<*const ()>() {
            no_deref
        } else {
            deref
        }
    };
}
