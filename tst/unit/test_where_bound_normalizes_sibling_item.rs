// Selecting `LogService<Inner>: Service<()>` binds `U` through the impl's own
// where clause `S: Service<T, Response = U>`, which normalizes another
// associated item of the same trait while the outer goal is still active.  A
// cycle answer there left `U` unbound and no method was found.

trait Service<Request> {
    type Response;
    fn call(&mut self, req: Request) -> Self::Response;
}

struct LogService<S> {
    inner: S,
}

trait Marker {
    type Item<'a>;
}

impl<T, U, S> Service<T> for LogService<S>
where
    S: Service<T, Response = U>,
    U: Marker + 'static,
    for<'a> U::Item<'a>: std::fmt::Debug,
{
    type Response = S::Response;

    fn call(&mut self, req: T) -> Self::Response {
        self.inner.call(req)
    }
}

struct Inner;
struct Resp(u32);
impl Marker for Resp {
    type Item<'a> = RespItem<'a>;
}

#[derive(Debug)]
struct RespItem<'a>(&'a ());

impl Service<()> for Inner {
    type Response = Resp;
    fn call(&mut self, _req: ()) -> Resp {
        Resp(7)
    }
}

fn main() {
    let mut service = LogService { inner: Inner };
    assert_eq!(service.call(()).0, 7);
}
