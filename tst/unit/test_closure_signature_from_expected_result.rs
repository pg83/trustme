// The generic parameters of a method appear both in the closure parameter it
// takes and in its return type. When the call's expected result pins those
// parameters, the closure's own signature follows from them - an identity
// closure has nothing else to fix its argument or return type, so that
// expectation has to reach it or type checking never stabilises.

struct Program;

impl<'a> Program {
    fn closure<TArg, TRet>(&'a self, f: impl Fn(TArg) -> TRet + 'a) -> &'a dyn Fn(TArg) -> TRet {
        Box::leak(Box::new(f))
    }

    fn take(&'a self, f: &'a dyn Fn(u32) -> u32) -> u32 {
        f(4)
    }

    fn identity_through_call(&'a self) -> u32 {
        self.take(self.closure(move |u| u))
    }

    fn identity_through_binding(&'a self) -> u32 {
        let f: &'a dyn Fn(u32) -> u32 = self.closure(move |u| u);
        f(6)
    }
}

fn main() {
    let program = Program;
    assert_eq!(program.identity_through_call(), 4);
    assert_eq!(program.identity_through_binding(), 6);
}
