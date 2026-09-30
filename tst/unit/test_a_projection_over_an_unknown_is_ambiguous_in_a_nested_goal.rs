// tower-http's `Decompression::new(client)` over hyper-util's
// `Client::builder(..).build_http()` leaves the client's body type open until
// `client.ready().await.unwrap().call(req)`. Selecting `ready` asks for
// `Decompression<Client<?B>>: ServiceExt<?R>`, whose nested goals reach the
// client's `impl<B> Service<Request<B>> for Client<B> where B: Body, B::Data:
// Send`. Upstream normalizes `<?B as Body>::Data` to a fresh variable - its
// self is an inference variable - so `Send` of it is ambiguous. We read the
// projection over the unknown as rigid, found no `Send` for it and dropped
// the only impl: "type annotations needed".
use std::marker::PhantomData;

trait Body {
    type Data;
}

impl Body for u32 {
    type Data = u8;
}

struct Request<B>(B);

trait Service<R> {
    type Error;
    fn call(&mut self, req: R) -> u32;
}

struct Client<B>(PhantomData<fn(B)>);

impl<B> Service<Request<B>> for Client<B>
where
    B: Body,
    B::Data: Send,
{
    type Error = String;
    fn call(&mut self, _: Request<B>) -> u32 {
        7
    }
}

struct Decompression<S>(S);

impl<S, B> Service<Request<B>> for Decompression<S>
where
    S: Service<Request<B>>,
{
    type Error = S::Error;
    fn call(&mut self, req: Request<B>) -> u32 {
        self.0.call(req) + 1
    }
}

trait Fut {
    type Output;
    fn get(self) -> Self::Output;
}

struct Ready<'a, T, R>(&'a mut T, PhantomData<fn(R)>);

impl<'a, T, R> Fut for Ready<'a, T, R>
where
    T: Service<R>,
{
    type Output = Result<&'a mut T, T::Error>;
    fn get(self) -> Self::Output {
        Ok(self.0)
    }
}

trait ServiceExt<R>: Service<R> {
    fn ready(&mut self) -> Ready<'_, Self, R>
    where
        Self: Sized,
    {
        Ready(self, PhantomData)
    }
}

impl<T: ?Sized, R> ServiceExt<R> for T where T: Service<R> {}

fn build<B>() -> Client<B> {
    Client(PhantomData)
}

fn main() {
    let mut client = Decompression(build());
    assert_eq!(client.ready().get().unwrap().call(Request(5u32)), 8);
}
