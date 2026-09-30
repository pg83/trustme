use std::future::Future;

pub struct Request(pub Vec<u8>);

pub trait FromRequest<S, M = ()>: Sized {
    type Rejection;
    fn from_request(req: Request, state: &S) -> impl Future<Output = Result<Self, Self::Rejection>> + Send;
}

pub trait RequestExt: Sized {
    fn extract<E, M>(self) -> impl Future<Output = Result<E, E::Rejection>> + Send
    where
        E: FromRequest<(), M> + 'static,
        M: 'static;

    fn extract_with_state<E, S, M>(self, state: &S) -> impl Future<Output = Result<E, E::Rejection>> + Send
    where
        E: FromRequest<S, M> + 'static,
        S: Send + Sync;
}

impl RequestExt for Request {
    fn extract<E, M>(self) -> impl Future<Output = Result<E, E::Rejection>> + Send
    where
        E: FromRequest<(), M> + 'static,
        M: 'static,
    {
        self.extract_with_state(&())
    }

    fn extract_with_state<E, S, M>(self, state: &S) -> impl Future<Output = Result<E, E::Rejection>> + Send
    where
        E: FromRequest<S, M> + 'static,
        S: Send + Sync,
    {
        E::from_request(self, state)
    }
}

