//@ crate-type: lib
//@ compile-flags: -Zmir-opt-level=2 -Zdump-mir
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

struct Token;

impl Drop for Token {
    fn drop(&mut self) {}
}

pub fn make(choose: bool) -> impl std::ops::Coroutine<Yield = (), Return = ()> {
    #[coroutine]
    move || {
        let mut token = Token;
        if choose {
            loop {
                if false {
                    drop(token);
                    token = Token;
                    break;
                }
                yield ();
                break;
            }
        } else {
            loop {
                if false {
                    drop(token);
                    token = Token;
                    break;
                }
                yield ();
                break;
            }
        }
    }
}
