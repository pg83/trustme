// lambda-extension's `run<E>` has `E: Service<LambdaEvent>` and
// `E::Future: Future<Output = Result<(), E::Error>>`; tower's `Service`
// declares `type Future: Future<Output = Result<Self::Response, Self::Error>>`.
// Proving `E::Future: Future<Output = Result<(), E::Error>>` (for
// `Extension::new`'s where-clause) found the item bound as an environment
// clause too, on equal terms with the written one, and the two outputs left
// the goal ambiguous. Upstream's item bounds are alias bounds
// (`assemble_alias_bound_candidates`, `assemble_candidates_from_projected_tys`)
// and a where-clause that applies is preferred over them.
use std::future::Future;

pub trait Service<Request> {
    type Response;
    type Error;
    type Future: Future<Output = Result<Self::Response, Self::Error>>;
    fn call(&mut self, req: Request) -> Self::Future;
}

pub struct Event;

pub struct Extension<E> {
    processor: E,
}

impl<E> Extension<E>
where
    E: Service<Event>,
    E::Future: Future<Output = Result<(), E::Error>>,
{
    pub fn new(processor: E) -> Self {
        Extension { processor }
    }

    pub async fn run(mut self) -> Result<(), E::Error> {
        self.processor.call(Event).await
    }
}

pub async fn run<E>(processor: E) -> Result<(), E::Error>
where
    E: Service<Event>,
    E::Future: Future<Output = Result<(), E::Error>>,
{
    Extension::new(processor).run().await
}

fn main() {}
