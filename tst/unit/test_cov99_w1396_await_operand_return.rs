async fn stop_before_await() {
    std::future::ready::<()>(return).await;
}

fn main() {
    drop(stop_before_await());
}
