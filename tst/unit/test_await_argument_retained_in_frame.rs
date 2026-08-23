async fn leaf(_value: [u8; 16]) {}

async fn wrap(future: impl std::future::Future<Output = ()>) {
    future.await;
}

fn main() {
    let future = wrap(wrap(wrap(wrap(wrap(leaf([0; 16]))))));
    if std::mem::size_of_val(&future) <= 550 {
        std::process::exit(1);
    }
}
