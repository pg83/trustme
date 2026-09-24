// tracing's `event!` passes `{ use ..; fields.value_set_all(&[Some(&display(format!(..)) as &dyn Value)]) }`
// to a closure it calls at once. Before edition 2024 a temporary made by a
// block's tail expression takes the temporary scope of the block itself -
// here the enclosing statement - and only the bodies of arms, loops and
// functions end their temporaries at their own close. The formatted
// String was freed before the closure read it.
use std::cell::RefCell;

thread_local! {
    static LOG: RefCell<Vec<&'static str>> = RefCell::new(Vec::new());
}

fn log(s: &'static str) {
    LOG.with(|l| l.borrow_mut().push(s));
}

struct Loud(&'static str);

impl Drop for Loud {
    fn drop(&mut self) {
        log(self.0);
    }
}

fn call(x: &Loud) {
    assert!(!x.0.is_empty());
    log("call");
}

fn main() {
    call({ &Loud("argument") });
    log("statement end");
    (|x: &[Option<&Loud>]| {
        assert!(!x[0].unwrap().0.is_empty());
        log("closure call")
    })({ &[Some(&Loud("closure"))] });
    log("statement end");
    let n = if true { call(&Loud("arm")); 1 } else { 2 };
    log("statement end");
    assert_eq!(n, 1);
    LOG.with(|l| {
        assert_eq!(
            *l.borrow(),
            [
                "call", "argument", "statement end",
                "closure call", "closure", "statement end",
                "call", "arm", "statement end",
            ]
        )
    });
}
