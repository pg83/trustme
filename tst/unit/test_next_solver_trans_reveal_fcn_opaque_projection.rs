// Regression: with the goal solver, `Foo { _func: bar, .. }` keeps the
// fn-def type `fn{bar}` (legacy inferred a fn-ptr), so the field type
// `<<T as Func>::Ret as Id>::Assoc` normalises through bar's
// return-position opaque during trans. Trans must run in reveal-all mode
// (rustc's PostAnalysisNormalize): the opaque is replaced by its hidden
// type instead of reaching layout/mangling ("Non-encodable type impl ...").
// Distilled from rust_1_90/upstream/mir/validate/needs-reveal-all.rs.

use std::hint::black_box;

trait Func {
    type Ret: Id;
}

trait Id {
    type Assoc;
}
impl Id for u32 {
    type Assoc = u32;
}

impl<F: FnOnce() -> R, R: Id> Func for F {
    type Ret = R;
}

fn bar() -> impl Copy + Id {
    0u32
}

struct Foo<T: Func> {
    _func: T,
    value: Option<<<T as Func>::Ret as Id>::Assoc>,
}

fn main() {
    let mut fn_def = black_box(Foo {
        _func: bar,
        value: None,
    });
    let fn_ptr = black_box(Foo {
        _func: bar as fn() -> _,
        value: None,
    });

    fn_def.value = fn_ptr.value;
    black_box(fn_def);
}
