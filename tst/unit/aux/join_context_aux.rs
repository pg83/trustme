pub struct FnContext {
    migrated: bool,
}

impl FnContext {
    fn new(migrated: bool) -> Self {
        FnContext { migrated }
    }

    pub fn migrated(&self) -> bool {
        self.migrated
    }
}

pub fn in_worker<F, R>(f: F) -> R
where
    F: FnOnce(bool) -> R + Send,
    R: Send,
{
    f(true)
}

pub fn join_context<A, B, RA, RB>(oper_a: A, oper_b: B) -> (RA, RB)
where
    A: FnOnce(FnContext) -> RA + Send,
    B: FnOnce(FnContext) -> RB + Send,
    RA: Send,
    RB: Send,
{
    #[inline]
    fn call_a<R>(f: impl FnOnce(FnContext) -> R, injected: bool) -> impl FnOnce() -> R {
        move || f(FnContext::new(injected))
    }

    #[inline]
    fn call_b<R>(f: impl FnOnce(FnContext) -> R) -> impl FnOnce(bool) -> R {
        move |migrated| f(FnContext::new(migrated))
    }

    in_worker(|injected| {
        let job_b = call_b(oper_b);
        let result_a = call_a(oper_a, injected)();
        let result_b = job_b(false);
        (result_a, result_b)
    })
}
