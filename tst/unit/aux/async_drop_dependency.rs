#![feature(async_drop)]
#![allow(incomplete_features)]

use std::sync::atomic::{AtomicU32, Ordering};

pub static SYNC_DROPS: AtomicU32 = AtomicU32::new(0);
pub static ASYNC_DROPS: AtomicU32 = AtomicU32::new(0);

pub struct HasDrop;

impl Drop for HasDrop {
    fn drop(&mut self) {
        SYNC_DROPS.fetch_add(1, Ordering::SeqCst);
    }
}

pub struct MongoDrop;

impl MongoDrop {
    pub async fn new() -> Result<Self, HasDrop> {
        Ok(Self)
    }
}

impl Drop for MongoDrop {
    fn drop(&mut self) {
        SYNC_DROPS.fetch_add(1, Ordering::SeqCst);
    }
}

impl std::future::AsyncDrop for MongoDrop {
    async fn drop(self: std::pin::Pin<&mut Self>) {
        ASYNC_DROPS.fetch_add(1, Ordering::SeqCst);
    }
}
