use std::future::Future;
use std::rc::Rc;

pub struct Receiver(pub u32);

impl Receiver {
    pub async fn ready(&self) -> u32 {
        self.0
    }
}

pub fn ready_now(value: u32) -> impl Future<Output = u32> {
    std::future::ready(value)
}

pub async fn local_count() -> usize {
    let shared = Rc::new(5usize);
    std::future::ready(()).await;
    Rc::strong_count(&shared) + *shared
}
