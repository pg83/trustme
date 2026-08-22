//@ crate-type: lib

use std::future::{ready, IntoFuture, Ready};

struct Builder(u32);

impl IntoFuture for Builder {
    type Output = u32;
    type IntoFuture = Ready<u32>;

    fn into_future(self) -> Self::IntoFuture {
        ready(self.0)
    }
}

async fn accepted() -> u32 {
    Builder(42).await
}
