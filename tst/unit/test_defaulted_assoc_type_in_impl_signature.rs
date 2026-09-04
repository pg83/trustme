#![feature(associated_type_defaults)]

// A trait's associated type may carry a default written in terms of the other
// associated types, and an impl that leaves it out gets that default with its
// own `Self` substituted in.  Checking `get_cx` against the trait needs
// `Self::CtxtBrw` normalized to `&MyCtxt` through both the default and
// `MyEmitter`'s own `Ctxt`; stopping at `<MyEmitter as Emitter>::Ctxt` rejected
// the impl as not matching the trait.

trait Emitter<'a> {
    type Ctxt: 'a;
    type CtxtBrw: 'a = &'a Self::Ctxt;

    fn get_cx(&'a self) -> Self::CtxtBrw;
}

struct MyCtxt(u32);

struct MyEmitter {
    ctxt: MyCtxt,
}

impl<'a> Emitter<'a> for MyEmitter {
    type Ctxt = MyCtxt;

    fn get_cx(&'a self) -> &'a MyCtxt {
        &self.ctxt
    }
}

fn main() {
    let emitter = MyEmitter { ctxt: MyCtxt(7) };
    assert_eq!(emitter.get_cx().0, 7);
}
