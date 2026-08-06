// Extracted from library/core/src/clone.rs:365
#![feature(clone_to_uninit)]
use std::clone::CloneToUninit;
use std::mem::offset_of;
use std::rc::Rc;

#[derive(PartialEq)]
struct MyDst<T: ?Sized> {
    label: String,
    contents: T,
}

unsafe impl<T: ?Sized + CloneToUninit> CloneToUninit for MyDst<T> {
    unsafe fn clone_to_uninit(&self, dest: *mut u8) {
        // The offset of `self.contents` is dynamic because it depends on the alignment of T
        // which can be dynamic (if `T = dyn SomeTrait`). Therefore, we have to obtain it
        // dynamically by examining `self`, rather than using `offset_of!`.
        //
        // SAFETY: `self` by definition points somewhere before `&self.contents` in the same
        // allocation.
        let offset_of_contents = unsafe {
            (&raw const self.contents).byte_offset_from_unsigned(self)
        };

        // Clone the *sized* fields of `self` (just one, in this example).
        // (By cloning this first and storing it temporarily in a local variable, we avoid
        // leaking it in case of any panic, using the ordinary automatic cleanup of local
        // variables. Such a leak would be sound, but undesirable.)
        let label = self.label.clone();

        // SAFETY: The caller must provide a `dest` such that these field offsets are valid
        // to write to.
        unsafe {
            // Clone the unsized field directly from `self` to `dest`.
            self.contents.clone_to_uninit(dest.add(offset_of_contents));

            // Now write all the sized fields.
            //
            // Note that we only do this once all of the clone() and clone_to_uninit() calls
            // have completed, and therefore we know that there are no more possible panics;
            // this ensures no memory leaks in case of panic.
            dest.add(offset_of!(Self, label)).cast::<String>().write(label);
        }
        // All fields of the struct have been initialized; therefore, the struct is initialized,
        // and we have satisfied our `unsafe impl CloneToUninit` obligations.
    }
}

fn main() {
    // Construct MyDst<[u8; 4]>, then coerce to MyDst<[u8]>.
    let first: Rc<MyDst<[u8]>> = Rc::new(MyDst {
        label: String::from("hello"),
        contents: [1, 2, 3, 4],
    });

    let mut second = first.clone();
    // make_mut() will call clone_to_uninit().
    for elem in Rc::make_mut(&mut second).contents.iter_mut() {
        *elem *= 10;
    }

    assert_eq!(first.contents, [1, 2, 3, 4]);
    assert_eq!(second.contents, [10, 20, 30, 40]);
    assert_eq!(second.label, "hello");
}
