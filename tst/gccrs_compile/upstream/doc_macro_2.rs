// { dg-additional-options "-w" }

macro_rules! foo {
    ($e:expr, $($t:tt)*) => {
        #[doc = $e]
        $($t)*
    }
}

macro_rules! bar {
    () => { "bar" }
}

struct S;

trait T {
    fn f();
}

impl T for S {
    foo!(bar!(), fn f() {});
}
