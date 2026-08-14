#![feature(return_type_notation)]

use std::future::Future;

trait HealthCheck {
    async fn check(&mut self) -> bool;
}

fn spawn<F: Future + Send + 'static>(_: F) {}

async fn run<HC>(hc: HC)
where
    HC: HealthCheck<check(..): Send> + Send + 'static,
{
    spawn(async move {
        let mut hc = hc;
        let _ = hc.check().await;
    });
}

fn main() {}
