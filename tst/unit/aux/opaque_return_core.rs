use std::future::Future;

pub struct Request(pub Vec<u8>);

pub trait FromRequest: Sized {
    type Rejection;
    fn from_request(req: Request) -> impl Future<Output = Result<Self, Self::Rejection>> + Send;
}

pub fn extract<E: FromRequest>(req: Request) -> impl Future<Output = Result<E, E::Rejection>> + Send {
    E::from_request(req)
}
