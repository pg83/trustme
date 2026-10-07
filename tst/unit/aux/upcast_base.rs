use std::sync::Arc;

pub trait Upcast {
    fn upcast_arc<'a>(self: Arc<Self>) -> Arc<dyn Base + 'a>
    where
        Self: 'a;
}

impl<T> Upcast for T
where
    T: Base + Sized,
{
    fn upcast_arc<'a>(self: Arc<Self>) -> Arc<dyn Base + 'a>
    where
        Self: 'a,
    {
        self
    }
}

pub trait Base: Upcast + Send + Sync {
    fn id(&self) -> u32;
}
