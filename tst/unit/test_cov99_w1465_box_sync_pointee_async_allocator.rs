//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(async_drop, allocator_api)]
#![allow(incomplete_features)]

use std::alloc::{AllocError, Allocator, Global, Layout};
use std::future::AsyncDrop;
use std::pin::Pin;
use std::ptr::NonNull;

pub struct SyncPayload(pub u8);
impl Drop for SyncPayload {
    fn drop(&mut self) {}
}

pub struct AsyncAllocator;
unsafe impl Allocator for AsyncAllocator {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        Global.allocate(layout)
    }
    unsafe fn deallocate(&self, pointer: NonNull<u8>, layout: Layout) {
        Global.deallocate(pointer, layout);
    }
}
impl Drop for AsyncAllocator {
    fn drop(&mut self) {}
}
impl AsyncDrop for AsyncAllocator {
    async fn drop(self: Pin<&mut Self>) {}
}

pub async fn release(_owned: Box<SyncPayload, AsyncAllocator>) {}
