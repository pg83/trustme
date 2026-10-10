// httptest 0.16: `ctx.chain(&mut self.0, &headers)` with
// `fn chain<M, I>(&mut self, matcher: &mut M, input: &I) where M:
// Matcher<I> + ?Sized, I: Debug + ?Sized` and `M: Matcher<[KV]>` in scope.
// rustc resolves the obligations before coercing each argument: once the
// first argument fixes `M`, `M: Matcher<?I>` has the where-clause alone and
// makes `I = [KV]`, and `&Vec<KV>` then dereferences to it. `I: Debug`, on a
// still unknown `I`, decides nothing and does not stop that.
use std::fmt;

pub trait Matcher<IN: ?Sized> {
    fn matches(&mut self, input: &IN, ctx: &mut Ctx) -> bool;
}

pub struct Ctx {
    depth: usize,
}

impl Ctx {
    pub fn chain<M, I>(&mut self, matcher: &mut M, input: &I) -> bool
    where
        M: Matcher<I> + ?Sized,
        I: fmt::Debug + ?Sized,
    {
        self.depth += 1;
        matcher.matches(input, self)
    }
}

struct Len;

impl Matcher<[u8]> for Len {
    fn matches(&mut self, input: &[u8], _: &mut Ctx) -> bool {
        input.len() == 2
    }
}

struct Headers<M>(M);

impl<M> Matcher<String> for Headers<M>
where
    M: Matcher<[u8]>,
{
    fn matches(&mut self, input: &String, ctx: &mut Ctx) -> bool {
        let bytes: Vec<u8> = input.bytes().collect();
        ctx.chain(&mut self.0, &bytes)
    }
}

fn main() {
    let mut ctx = Ctx { depth: 0 };
    assert!(Headers(Len).matches(&"ab".to_string(), &mut ctx));
    assert_eq!(ctx.depth, 1);
}
