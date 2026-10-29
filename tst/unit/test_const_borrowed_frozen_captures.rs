#![feature(const_async_blocks, coroutines, stmt_expr_attributes)]
struct Token(u8);
impl Token { fn value(&self) -> u8 { self.0 } }
fn frozen<T: ?Sized + 'static>(_: &'static T) {}
fn main() {
    let plain = const { &|| 7u8 };
    let copied = const { let n = 9u8; &move || n + 1 };
    let owned = const { let n = Token(12); &move || n.value() + 1 };
    let nested = const {
        let token = Token(15);
        let inner = move || token.value();
        &move || inner() + 1
    };
    let partial = const {
        let pair = (Token(21), std::cell::Cell::new(99u8));
        &move || pair.0.value()
    };
    assert_eq!((plain(), copied(), owned(), nested(), partial()), (7, 10, 13, 16, 21));
    frozen(owned);
    frozen(nested);
    frozen(partial);
    let future = const { let n = Token(31); &async move { n.value() } };
    let coroutine = const { let n = Token(41); &(#[coroutine] move || { yield n.value(); 42u8 }) };
    frozen(future);
    frozen(coroutine);
    assert!(std::mem::size_of_val(future) >= std::mem::size_of::<Token>());
    assert!(std::mem::size_of_val(coroutine) >= std::mem::size_of::<Token>());
}
