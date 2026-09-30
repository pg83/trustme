use std::future::Future;

pub struct Request(pub Vec<u8>);

pub mod private {
    pub struct ViaRequest;
}

pub trait FromRequest<S, M = private::ViaRequest>: Sized {
    type Rejection;
    fn from_request(req: Request, state: &S) -> impl Future<Output = Result<Self, Self::Rejection>> + Send;
}

pub struct Bytes(pub Vec<u8>);

impl<S> FromRequest<S> for Bytes
where
    S: Send + Sync,
{
    type Rejection = String;
    async fn from_request(req: Request, _state: &S) -> Result<Self, String> {
        if req.0.is_empty() { Err("empty".to_string()) } else { Ok(Bytes(req.0)) }
    }
}
