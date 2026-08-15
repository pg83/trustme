//@ crate-type: lib
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Equal<const LEFT: usize, const RIGHT: usize>;

trait True {}

impl<const VALUE: usize> True for Equal<VALUE, VALUE> {}

trait Tokenize {
    type Untokenized;
}

struct Item<T>(T);
struct Token<T>(core::marker::PhantomData<T>);
struct Arena;

impl Arena {
    fn tokenize<T, U>(&self, _: Item<&mut T>) -> Token<U>
    where
        T: Tokenize<Untokenized = U>,
        Equal<{ core::mem::size_of::<T>() }, { core::mem::size_of::<U>() }>: True,
    {
        Token(core::marker::PhantomData)
    }
}

struct Value;

impl Tokenize for Value {
    type Untokenized = Value;
}

struct Input<'a>(Option<Item<&'a mut Value>>);
struct Output(Option<Token<Value>>);

fn convert(arena: &Arena, input: Input<'_>) -> Output {
    Output(input.0.map(|item| arena.tokenize(item)))
}

fn main() {}
