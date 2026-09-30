use std::future::Future;

pub trait FromParts<S>: Sized {
    fn from_parts(parts: &mut u32, state: &S) -> impl Future<Output = Option<Self>> + Send;
}
