//@ compile-fail: AsyncDrop impl requires a Drop impl
#![feature(async_drop)]
struct Resource;
impl std::future::AsyncDrop for Resource {
    async fn drop(self: std::pin::Pin<&mut Self>) {}
}
fn main() {}
