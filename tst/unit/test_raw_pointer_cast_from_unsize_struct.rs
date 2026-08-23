//@ crate-type: lib

#![no_std]

use core::ffi::c_void;
use core::mem::ManuallyDrop;

pub fn erase<F>(data: *mut ManuallyDrop<F>) -> *mut c_void {
    data as *mut c_void
}
