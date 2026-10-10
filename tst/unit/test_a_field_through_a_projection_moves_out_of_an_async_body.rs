// rocket's `impl Rocket<Build> { async fn ignite(self) .. }` moves
// `self.0.figment` out, where `struct Rocket<P: Phase>(P::State)`. The field
// `.0` is declared `P::State`; upstream normalizes a field's declared type
// before it reads the next field off it (`structurally_normalize` in
// `check_field`). The capture analysis of the async body took `.0` as the bare
// `<Build as Phase>::State` and found no `figment` on it ("Getting field on
// invalid type").
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

pub trait Phase {
    type State;
}

pub struct Building {
    pub figment: String,
    pub fairings: u32,
}

pub enum Build {}

impl Phase for Build {
    type State = Building;
}

pub struct Rocket<P: Phase>(P::State);

impl Rocket<Build> {
    async fn ignite(self) -> (String, u32) {
        std::future::ready(()).await;
        (self.0.figment, self.0.fairings)
    }
}

fn block_on<F: Future>(future: F) -> F::Output {
    let mut future = pin!(future);
    let mut context = Context::from_waker(Waker::noop());
    loop {
        if let Poll::Ready(value) = future.as_mut().poll(&mut context) {
            return value;
        }
    }
}

fn main() {
    let rocket: Rocket<Build> = Rocket(Building { figment: "fig".to_string(), fairings: 3 });
    assert_eq!(block_on(rocket.ignite()), ("fig".to_string(), 3));
}
